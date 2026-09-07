#include "UISystem.h"

#include <algorithm>
#include <iterator>
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

namespace
{
/** Render ONE data-driven control. Dispatches on EUIControlType; interaction
 *  (button/checkbox/slider) queues an FUIEvent (routed by control Id) to Sys, so the
 *  game thread can write the new value back. Runs on the render worker inside the
 *  ImGui frame. */
void DrawControl(const FUIControl& C, FUISystem& Sys)
{
	switch (C.Type)
	{
	case EUIControlType::Label:
		ImGui::Text("%s", C.Text.c_str());
		break;
	case EUIControlType::Text:
		ImGui::TextWrapped("%s", C.Text.c_str());
		break;
	case EUIControlType::Image:
		if (C.ResourceId != 0)
		{
			ImGui::Image((ImTextureID)(std::uintptr_t)C.ResourceId, ImVec2(C.V0, C.V1));
		}
		break;
	case EUIControlType::Button:
		if (ImGui::Button(C.Text.c_str()))
		{
			Sys.PushUIEvent(FUIEvent{ C.Id, EUIControlType::Button, 0.f, 0.f });
		}
		break;
	case EUIControlType::Slider:
		{
			float Val = C.V0;
			if (ImGui::SliderFloat("##slider", &Val, 0.f, C.V1))
			{
				Sys.PushUIEvent(FUIEvent{ C.Id, EUIControlType::Slider, Val, C.V1 });
			}
		}
		break;
	case EUIControlType::Checkbox:
		{
			bool Checked = C.V0 != 0.f;
			if (ImGui::Checkbox(C.Text.c_str(), &Checked))
			{
				Sys.PushUIEvent(FUIEvent{ C.Id, EUIControlType::Checkbox, Checked ? 1.f : 0.f, 1.f });
			}
		}
		break;
	case EUIControlType::Separator:
		ImGui::Separator();
		break;
	}
}
} // namespace

void FUISystem::OnInstalled(FGameWorld& World)
{
	GUISystem = this;

	// UI orchestration: game-side UI components arrange what to draw by submitting
	// draw CLOSURES to the UIBuilder (see Update below). FUIFeature pulls them each
	// frame and runs them between NewFrame and Render -- so every ImGui call the game
	// makes stays on the ImGui context's single owner thread.

	// Spawn a few widget entities: each gets an FTransform (screen anchor) plus an
	// FUIWidget (widget state) -- the world's SoA component pools store them by
	// entity Index. Each widget OWNS its own control orchestration, so "the same
	// component type" renders different content per entity.
	constexpr FTransform Anchors[] = {
		FTransform{50.f,  60.f,  0.f},
		FTransform{370.f, 220.f, 0.f},
		FTransform{640.f, 420.f, 0.f},
	};
	for (std::size_t I = 0; I < std::size(Anchors); ++I)
	{
		const FTransform& Anchor = Anchors[I];
		FEntity E = World.CreateEntity();
		World.AddComponent<FTransform>(E, Anchor);

		FUIWidget W;
		W.X = Anchor.X;
		W.Y = Anchor.Y;
		W.Width = 150.f;
		W.Height = 80.f;
		W.bVisible = true;

		// Distinct control orchestration per entity -- content differs, type is shared.
		switch (I)
		{
		case 0:
			W.Controls = {
				{ EUIControlType::Label,    "widget 0 title" },
				{ EUIControlType::Separator },
				{ EUIControlType::Text,     "This widget shows static text." },
			};
			break;
		case 1:
			W.Controls = {
				{ EUIControlType::Label,    "widget 1" },
				{ EUIControlType::Button,   "press me" },
				{ EUIControlType::Checkbox, "enabled", 0u, 0u, 1.f, 0.f },
			};
			break;
		case 2:
			W.Controls = {
				{ EUIControlType::Label,    "widget 2" },
				{ EUIControlType::Separator },
				{ EUIControlType::Slider,   "volume", 0u, 0u, 0.5f, 1.f },
			};
			break;
		}

		// Assign a routing id to every control: render-thread interactions carry the id
		// back (via FUIEvent) so the game thread can locate the control and write the
		// new value into this widget's component.
		for (FUIControl& C : W.Controls)
		{
			C.Id = NextControlId++;
		}

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

	// Apply queued UI interactions (produced on the render worker last frame). The
	// widget component is the single source of truth, so a control interaction writes
	// its new value back into the owning widget's FUIControl (data-driven state).
	for (const FUIEvent& Ev : DrainUIEvents())
	{
		for (FEntity E : Widgets)
		{
			if (FUIWidget* W = World.GetComponent<FUIWidget>(E))
			{
				for (FUIControl& C : W->Controls)
				{
					if (C.Id != Ev.ControlId)
					{
						continue;
					}
					switch (Ev.Type)
					{
					case EUIControlType::Slider:
					case EUIControlType::Checkbox:
						C.V0 = Ev.A;
						break;
					case EUIControlType::Button:
						// Button has no persisted value -- wire your action here (e.g.
						// toggle a flag, spawn an entity, queue a command).
						break;
					default:
						break;
					}
				}
			}
		}
	}

	// Widget positions stay static (anchored in screen space). Moving them every frame
	// makes the just-repositioned ImGui windows jitter/flicker in immediate mode.

	// UI components arrange what to draw by submitting draw CLOSURES to the UIBuilder.
	// Each closure captures only DATA (values, no ImGui state); it runs on the render
	// worker inside the ImGui frame (between NewFrame and Render). ImGui is
	// immediate-mode, so these closure bodies ARE the real Begin/Image/End calls --
	// they are not recorded here, merely collected and executed later on the worker.
	//
	// a) one window per widget entity -- render EVERY FUIWidget component in the
	//    world (the default widget created by FGameWorld::Initialize, plus any the
	//    world systems spawn). The widget OWNS a control orchestration list; each
	//    control (by EUIControlType) is rendered by DrawControl inside the widget's
	//    ImGui window. The control list is value-copied into the closure so the
	//    render worker sees a stable, independent snapshot (no shared state). `this`
	//    is captured only so the worker can queue interactions back to THIS system's
	//    event queue; it is safe because the render feature (which runs these
	//    closures) is uninstalled BEFORE this world system.
	{
		std::size_t I = 0;
		for (const FEntity E : World.GetAllWithComponent<FUIWidget>())
		{
			const FUIWidget* W = World.GetComponent<FUIWidget>(E);
			if (W == nullptr || !W->bVisible)
			{
				continue;
			}
			const float X = W->X, Y = W->Y, Wd = W->Width, Ht = W->Height;
			const std::vector<FUIControl> Controls = W->Controls;
				UIBuilder.Submit([I, X, Y, Wd, Ht, Controls, this]()
				{
					// Position only on the FIRST frame (Cond_Once); afterwards the window
					// owns its position, so the user can drag it. NoMove would pin it back.
					// NoTitleBar keeps the layout clean while the implicit title-bar strip
					// (still present) is the drag handle. NoResize keeps the size fixed.
					ImGui::SetNextWindowPos(ImVec2(X, Y), ImGuiCond_Once);
					ImGui::SetNextWindowSize(ImVec2(Wd, Ht), ImGuiCond_Always);
					const std::string Title = "widget " + std::to_string(I);
					if (ImGui::Begin(Title.c_str(), nullptr,
							ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
				{
					for (const FUIControl& C : Controls)
					{
						DrawControl(C, *this);
					}
				}
				ImGui::End();
			});
			++I;
		}
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
