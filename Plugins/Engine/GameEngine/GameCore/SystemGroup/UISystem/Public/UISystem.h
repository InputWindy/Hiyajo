#pragma once

#include "UISystemApi.h"
#include <GameWorld.h>
#include "FUIBuilder.h"
#include <Core/Delegate.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Maho
{
namespace GameWorld
{

/** UI control kinds -- one branch per kind in the UISystem's DrawControl dispatcher. */
enum class EUIControlType : std::uint8_t
{
	Label, Text, Image, Button, Slider, Checkbox, Separator
};

/**
 * One data-driven ImGui control. Type x data selects what to draw. Interaction
 * (button/checkbox/slider) does NOT call a stored callback -- the render worker
 * queues an FUIEvent (routed by Id) and the GAME thread drains + writes it back on
 * the next Update, so the control's state persists in the widget's component.
 */
struct FUIControl
{
	EUIControlType Type  = EUIControlType::Label;
	std::string    Text;             // label / button / checkbox text
	std::uint32_t  ResourceId = 0;   // image texture resource id (FName::GetId(), for Type::Image)
	std::uint32_t  Id = 0;           // routing id (assigned by UISystem at widget creation)
	float          V0 = 0.f;         // Type-dependent: slider value / checkbox state / image width
	float          V1 = 1.f;         // Type-dependent: slider max / image height
};

/** One UI interaction event, queued on the render thread and drained (written back to
 *  the owning widget control) by the game thread on the next Update. */
struct FUIEvent
{
	std::uint32_t   ControlId = 0;
	EUIControlType  Type = EUIControlType::Label;
	float           A = 0.f;   // slider value / checkbox state (0/1)
	float           B = 1.f;   // slider max
};

/**
 * ECS-side UI widget model. Pure game data -- the render layer (FUIFeature/ImGui)
 * consumes this upstream; this system only owns and animates it. Holds an ORDERED
 * control list: the UISystem widget pass renders each control (by EUIControlType)
 * inside the widget's ImGui window.
 */
struct MAHO_UISYSTEM_API FUIWidget
{
		float X = 0.f;          // screen-space position (left)
	float Y = 0.f;          // screen-space position (top)
	float Width = 100.f;
	float Height = 50.f;
	bool  bVisible = true;
	std::vector<FUIControl> Controls;   // ordered control orchestration
};

/**
 * GameWorld UI system -- a WORLD SYSTEM, installed into FGameWorld as a peer
 * layer (via the sub-landlord collector). It spawns a few widget entities on
 * install, animates them every update (demonstrating component read/write
 * through the world accessor), and tears them down on uninstall.
 *
 * It also owns the FUIBuilder -- the game->render UI command broker. Game-side UI
 * components (this system included) Submit draw CLOSURES to it; FUIFeature pulls them
 * once per render frame and executes the closures between ImGui::NewFrame and
 * ImGui::Render. So the game defines what to draw (the closures call ImGui::*), while
 * the render worker executes every ImGui call on its own frame. The game never touches
 * FRender; the render feature only owns the ImGui context + the FName->RDG mirror.
 */
class MAHO_UISYSTEM_API FUISystem : public FLayer<IOnInstalled, IProcessInput, IUpdate, IPreUnInstall>
{
	MAHO_DECLARE_LAYER(FUISystem, "UISystem.dll");

public:
	void OnInstalled(FGameWorld& World) override;
	void ProcessInput(FGameWorld& World) override;
	void Update(FGameWorld& World) override;
	void PreUnInstall(FGameWorld& World) override;

	/** Subscribe a handler to the "UI built this frame" broadcast. Thread-safe: the
	 *  subscriber (the render feature) may join from a render worker while the game
	 *  thread broadcasts. The handler runs ON THE BROADCAST THREAD (the game thread,
	 *  after Submit finished building the frame), RECEIVING the UIBuilder by reference --
	 *  so it can CopyFrame() directly, no GetUISystem() re-lookup (and no null-window
	 *  during teardown). Returns a subscription id; UnsubscribeUIHandler(id) removes ONLY
	 *  this handler. Callers MUST retain it until they unload. */
	uint64_t SubscribeUIHandler(std::function<void(const FUIBuilder&)> Handler);

	/** Remove ONLY the handler registered under this subscription id (mirror of
	 *  SubscribeUIHandler). No-op if the id is stale. */
	void UnsubscribeUIHandler(uint64_t Subscription);

	/** Queue a UI interaction event (called from the RENDER worker when a control is
	 *  activated). Thread-safe. The game thread drains it in Update and writes the new
	 *  value back to the owning widget control. */
	void PushUIEvent(FUIEvent E);

	/** Drain queued UI events (call from the GAME thread, typically at Update). The
	 *  returned list is owned by the caller. */
	std::vector<FUIEvent> DrainUIEvents();

private:

	FUIBuilder UIBuilder;   // game->render UI command broker (owned by this world system)
	std::vector<FEntity> Widgets;   // spawned widget entities
	float Time = 0.f;

	mutable std::mutex EventMutex;   // guards OnUIBuilt (bind vs broadcast race)
	TMulticastEvent<void(const FUIBuilder&)> OnUIBuilt;

	mutable std::mutex UIEventMutex;   // guards PendingUIEvents (render push vs game drain)
	std::vector<FUIEvent> PendingUIEvents;
	std::uint32_t NextControlId = 1;   // assigns FUIControl::Id (routing ids, never reused)
};

/** Global accessor to the UI system (cross-DLL, mirrors Resource::GetResourceSystem()).
 *  nullptr until the system is installed by FGameWorld. */
MAHO_UISYSTEM_API FUISystem* GetUISystem();

} // namespace GameWorld
} // namespace Maho
