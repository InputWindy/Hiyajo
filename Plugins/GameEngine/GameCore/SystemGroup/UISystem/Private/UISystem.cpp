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

namespace
{
// Draw one data-driven control from a widget's component. Interaction (Button/Checkbox/
// Slider) queues an FUIEvent (routed by ControlId); the game thread drains + writes the
// new value back to the owning control in Update. No stored callbacks -- the widget's
// component is the state.
void DrawControl(const FUIControl& C)
{
	switch (C.Type)
	{
		case EUIControlType::Label:     ImGui::Text("%s", C.Text.c_str()); break;
		case EUIControlType::Text:      ImGui::TextWrapped("%s", C.Text.c_str()); break;
		case EUIControlType::Separator: ImGui::Separator(); break;
		case EUIControlType::Image:
			ImGui::Image(static_cast<ImTextureID>(C.ResourceId), ImVec2(C.V0, C.V1));
			break;
		case EUIControlType::Button:
			if (ImGui::Button(C.Text.c_str()))
			{
				if (FUISystem* UI = GetUISystem()) { UI->PushUIEvent({ C.Id, C.Type, 1.f, 1.f }); }
			}
			break;
		case EUIControlType::Checkbox:
		{
			bool b = C.V0 > 0.f;
			if (ImGui::Checkbox(C.Text.c_str(), &b))
			{
				if (FUISystem* UI = GetUISystem()) { UI->PushUIEvent({ C.Id, C.Type, b ? 1.f : 0.f, 1.f }); }
			}
			break;
		}
		case EUIControlType::Slider:
		{
			float V = C.V0;
			if (ImGui::SliderFloat("##slider", &V, 0.f, C.V1))
			{
				if (FUISystem* UI = GetUISystem()) { UI->PushUIEvent({ C.Id, C.Type, V, C.V1 }); }
			}
			break;
		}
		default: break;
	}
}
} // namespace

void FUISystem::OnInstalled(FGameWorld& World)
{
	GUISystem = this;

	// UI is data-driven: spawn a demo FUIWidget as an ECS entity + component, written
	// through the world accessor. Update renders every FUIWidget entity each frame, so
	// the game defines UI by composing entities + FUIControls, not a hardcoded closure.
	FEntity E = World.CreateEntity();
	FUIWidget W;
	W.Name = "Game UI";
	W.AnchorX = 0.05f; W.AnchorY = 0.05f;
	W.SizeX = 0.30f;   W.SizeY = 0.25f;
	FUIControl Label;  Label.Type = EUIControlType::Label;  Label.Text = "Game UI placeholder";
	FUIControl Hint;   Hint.Type  = EUIControlType::Text;   Hint.Text  = "Drag this window by its title bar.";
	W.Controls.push_back(Label);
	W.Controls.push_back(Hint);
	World.AddComponent<FUIWidget>(E, W);
	DemoWidget = E;
}

void FUISystem::ProcessInput(FGameWorld&)
{
	// Input hook -- the world's IProcessInput stage. No game input backend yet;
	// left empty so the system demonstrates a stage it does not yet fill.
}

void FUISystem::Update(FGameWorld& World)
{
	// Write back render-side interaction events (a button/checkbox/slider was activated
	// on the render worker) into the owning widget control, so the component persists.
	WriteBackEvents();

	// UI is data-driven: render every FUIWidget entity as one ImGui window. Each closure
	// captures a SNAPSHOT (copy) of the widget, so the render worker runs against stable
	// data while the game thread may mutate the component the next frame. Position/size
	// follow display fraction (Always for size, Once for pos so it stays draggable).
	for (FEntity E : World.GetAllWithComponent<FUIWidget>())
	{
		const FUIWidget* Widget = World.GetComponent<FUIWidget>(E);
		if (!Widget)
		{
			continue;
		}
		const FUIWidget Snap = *Widget;
		UIBuilder.Submit([Snap]()
		{
			const ImVec2 DS = ImGui::GetIO().DisplaySize;
			ImGui::SetNextWindowPos(ImVec2(Snap.AnchorX * DS.x, Snap.AnchorY * DS.y), ImGuiCond_Once);
			ImGui::SetNextWindowSize(ImVec2(Snap.SizeX * DS.x, Snap.SizeY * DS.y), ImGuiCond_Always);
			if (ImGui::Begin(Snap.Name.c_str(), nullptr, ImGuiWindowFlags_NoResize))
			{
				for (const FUIControl& C : Snap.Controls)
				{
					DrawControl(C);
				}
			}
			ImGui::End();
		});
	}

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

void FUISystem::WriteBackEvents()
{
	// Write each queued interaction event back to the owning control (matched by Id),
	// so a button/checkbox/slider's state persists in the widget's component.
	FGameWorld* World = GetGameWorld();
	if (!World)
	{
		return;
	}
	for (const FUIEvent& Event : DrainUIEvents())
	{
		for (FEntity E : World->GetAllWithComponent<FUIWidget>())
		{
			FUIWidget* Widget = World->GetComponent<FUIWidget>(E);
			if (!Widget)
			{
				continue;
			}
			for (FUIControl& C : Widget->Controls)
			{
				if (C.Id == Event.ControlId)
				{
					C.V0 = Event.A;
					C.V1 = Event.B;
					break;
				}
			}
		}
	}
}

void FUISystem::PreUnInstall(FGameWorld& World)
{
	// Destroy the ECS widget entity this system spawned (close our own state -- do
	// not rely on collector ordering). The render feature (UIFeature) is uninstalled
	// BEFORE this world system (it depends on UISystem), so it pulls no further
	// closures after this point. Meanwhile the UIBuilder's leftover closures capture
	// only values/globals -- nothing dangles. The event is going away with this
	// system, so clear any residual subscriptions outright.
	if (DemoWidget.IsValid())
	{
		World.DestroyEntity(DemoWidget);
		DemoWidget = FEntity{};
	}
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
