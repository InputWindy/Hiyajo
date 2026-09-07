#include "UISystem.h"

#include <string>

#include "imgui.h"

namespace Maho
{
namespace GameWorld
{

// Global accessor target (cross-DLL, mirrors Resource::GResourceSystem). Set at
// OnInstalled, cleared at PreUnInstall; the render feature reads it to broadcast
// the UI frame orchestration.
FUISystem* GUISystem = nullptr;

void FUISystem::OnInstalled(FGameWorld&)
{
	GUISystem = this;

	// UI orchestration: the game-side UI submits ONE draw closure per frame (the
	// draggable text-box placeholder) to the UIBuilder. FUIFeature pulls it each
	// frame and runs it between NewFrame and Render -- so every ImGui call stays on
	// the single owner thread. The full data-driven control set (FUIControl +
	// DrawControl) belongs to the editor plugin; the game keeps only this closure.
}

void FUISystem::ProcessInput(FGameWorld&)
{
	// Input hook -- the world's IProcessInput stage. No game input backend yet;
	// left empty so the system demonstrates a stage it does not yet fill.
}

void FUISystem::Update(FGameWorld&)
{
	// The game side submits ONE draw closure that shows a single draggable text box
	// (the game UI placeholder). The closure captures only DATA; it runs on the render
	// worker inside the ImGui frame (between NewFrame and Render). The title bar is
	// NOT suppressed so it acts as the drag handle -- the window can be moved by the
	// user (SetNextWindowPos is Cond_Once, so the position is only clamped the first
	// frame; afterwards the window owns its position).
	UIBuilder.Submit([]()
	{
		ImGui::SetNextWindowPos(ImVec2(60.f, 60.f), ImGuiCond_Once);
		ImGui::SetNextWindowSize(ImVec2(280.f, 110.f), ImGuiCond_Once);
		if (ImGui::Begin("Game UI", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Game UI placeholder");
			ImGui::TextWrapped("Drag this window by its title bar.");
		}
		ImGui::End();
	});

	// THIS frame's UI is fully built: broadcast so subscribers (FUIFeature) copy the
	// UIBuilder batch to their OWN side. The broadcast + the copy run on the game
	// thread, AFTER Submit finished -- so the snapshot is a complete frame, never a
	// partial batch, and the render side always has a full set to draw. (The game
	// submit runs in an un-flushed, cross-frame-pipelined world update; WITHOUT this
	// event the render's Execute() would race the submit and see empty batches.)
	{
		std::lock_guard Lock(EventMutex);
		OnUIBuilt.Broadcast(UIBuilder);
	}
}

uint64_t FUISystem::SubscribeUIHandler(std::function<void(const FUIBuilder&)> Handler)
{
	if (!Handler)
	{
		return 0;
	}
	std::lock_guard Lock(EventMutex);
	return OnUIBuilt.Bind(std::move(Handler));
}

void FUISystem::UnsubscribeUIHandler(uint64_t Subscription)
{
	if (Subscription == 0)
	{
		return;
	}
	std::lock_guard Lock(EventMutex);
	OnUIBuilt.Unbind(Subscription);
}

void FUISystem::PushUIEvent(FUIEvent E)
{
	std::lock_guard Lock(UIEventMutex);
	PendingUIEvents.push_back(std::move(E));
}

std::vector<FUIEvent> FUISystem::DrainUIEvents()
{
	std::lock_guard Lock(UIEventMutex);
	std::vector<FUIEvent> Out = std::move(PendingUIEvents);
	PendingUIEvents.clear();
	return Out;
}

void FUISystem::PreUnInstall(FGameWorld&)
{
	// The render feature (UIFeature) is uninstalled BEFORE this world system (it
	// depends on UISystem), so it pulls no further closures after this point. The
	// UIBuilder's leftover closures capture only values/globals (no UISystem state),
	// and the builder is destroyed with this system -- nothing dangles. The event is
	// going away with this system, so clear any residual subscriptions outright.
	OnUIBuilt.RemoveAll();
	GUISystem = nullptr;
}

FUISystem* GetUISystem()
{
	return GUISystem;
}

} // namespace GameWorld
} // namespace Maho

// The C export FGameWorld looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_UISYSTEM_API Maho::FLayerBase* CreateLayer()
{
	return Maho::GameWorld::FUISystem::CreateLayer();
}
