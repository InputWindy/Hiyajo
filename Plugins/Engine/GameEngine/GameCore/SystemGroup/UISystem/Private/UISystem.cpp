#include "UISystem.h"

#include <algorithm>
#include <string>

#include <Resource.h>
#include <AssetTypes.h>

#include "imgui.h"

namespace Maho
{
namespace GameWorld
{

// Global accessor target (cross-DLL, mirrors Resource::GResourceSystem). Set at
// OnInstalled, cleared at PreUnInstall; the render feature reads it to broadcast
// the UI frame orchestration.
FUISystem* GUISystem = nullptr;

void FUISystem::OnInstalled(FGameWorld& World)
{
	GUISystem = this;

	// UI orchestration: game-side UI components arrange what to draw by submitting
	// draw CLOSURES to the UIBuilder (see Update below). FUIFeature pulls them each
	// frame and runs them between NewFrame and Render -- so every ImGui call the game
	// makes stays on the ImGui context's single owner thread.

	// Spawn a few widget entities: each gets an FTransform (screen anchor) plus an
	// FUIWidget (widget state) -- the world's SoA component pools store them by
	// entity Index.
	constexpr FTransform Anchors[] = {
		FTransform{50.f,  60.f,  0.f},
		FTransform{370.f, 220.f, 0.f},
		FTransform{640.f, 420.f, 0.f},
	};
	for (const FTransform& Anchor : Anchors)
	{
		FEntity E = World.CreateEntity();
		World.AddComponent<FTransform>(E, Anchor);

		FUIWidget W;
		W.X = Anchor.X;
		W.Y = Anchor.Y;
		W.Width = 140.f;
		W.Height = 70.f;
		W.bVisible = true;
		World.AddComponent<FUIWidget>(E, W);

		Widgets.push_back(E);
	}
}

void FUISystem::ProcessInput(FGameWorld&)
{
	// Input hook -- the world's IProcessInput stage. No game input backend yet;
	// left empty so the system demonstrates a stage it does not yet fill.
}

void FUISystem::Update(FGameWorld& World)
{
	Time += World.GetDeltaSeconds();

	// Widget positions stay static (anchored in screen space). Moving them every frame
	// makes the just-repositioned ImGui windows jitter/flicker in immediate mode.

	// UI components arrange what to draw by submitting draw CLOSURES to the UIBuilder.
	// Each closure captures only DATA (values, no ImGui state); it runs on the render
	// worker inside the ImGui frame (between NewFrame and Render). ImGui is
	// immediate-mode, so these closure bodies ARE the real Begin/Image/End calls --
	// they are not recorded here, merely collected and executed later on the worker.
	//
	// a) one window per widget entity -- demonstrable component-driven arrangement.
	for (std::size_t I = 0; I < Widgets.size(); ++I)
	{
		const FEntity E = Widgets[I];
		const FUIWidget* W = World.GetComponent<FUIWidget>(E);
		if (W == nullptr || !W->bVisible)
		{
			continue;
		}
		const float X = W->X, Y = W->Y, Wd = W->Width, Ht = W->Height;
		UIBuilder.Submit([I, X, Y, Wd, Ht]()
		{
			ImGui::SetNextWindowPos(ImVec2(X, Y), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(Wd, Ht), ImGuiCond_Always);
			const std::string Title = "widget " + std::to_string(I);
			if (ImGui::Begin(Title.c_str(), nullptr,
					ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
			{
				ImGui::Text("widget %zu @ %.0f,%.0f  %.0fx%.0f", I, X, Y, Wd, Ht);
			}
			ImGui::End();
		});
	}

	// b) the texture browser -- enumerates the FTexture assets the resource system
	//    has loaded (asset FName -> ImTextureID = the asset FName id).
	UIBuilder.Submit([]()
	{
		Resource::FResourceSystem* RS = Resource::GetResourceSystem();
		if (RS == nullptr)
		{
			return;
		}
		RS->ForEachResource([&](const Name::FName& AssetName, const Resource::FResource& Res)
		{
			const Resource::FTexture* Tex = dynamic_cast<const Resource::FTexture*>(&Res);
			if (Tex == nullptr || Tex->GetWidth() == 0 || Tex->GetHeight() == 0)
			{
				return;
			}
			const std::string Title = "texture: " + std::string(AssetName.ToString());
			const ImVec2& Disp = ImGui::GetIO().DisplaySize;
			ImGui::SetNextWindowSize(
				ImVec2(Disp.x * 0.9f, Disp.y * 0.9f),
				ImGuiCond_Always);
			if (ImGui::Begin(Title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
			{
				const ImVec2 Avail = ImGui::GetContentRegionAvail();
				const float IW = static_cast<float>(Tex->GetWidth());
				const float IH = static_cast<float>(Tex->GetHeight());
				constexpr float CaptionH = 20.0f;
				const float Scale = (std::min)(Avail.x / IW, (Avail.y - CaptionH) / IH);
				ImGui::Image((ImTextureID)AssetName.GetId(), ImVec2(IW * Scale, IH * Scale));
				ImGui::Text("asset=%s  %ux%u", std::string(AssetName.ToString()).c_str(), Tex->GetWidth(), Tex->GetHeight());
			}
			ImGui::End();
		});
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

void FUISystem::PreUnInstall(FGameWorld& World)
{
	for (FEntity E : Widgets)
	{
		World.DestroyEntity(E);
	}
	Widgets.clear();

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
