#pragma once

#include "UISystemApi.h"
#include <GameWorld.h>
#include <UIView.h>

#include <memory>

namespace Maho
{
namespace GameWorld
{

/**
 * ECS-side UI widget: the OWNER of one persistent declarative UI tree.
 *
 * The tree IS the widget. A `UI::FUIView` lives across frames and keeps its runtime
 * state (text buffers, toggles, scroll offsets); the world re-declares the frame's
 * content inside `FUIView::Edit()` each Update, and a node re-declared with the same id
 * AND type is REUSED (its state survives). The render side translates the whole tree
 * once per frame -- the game never issues an ImGui call.
 *
 * `shared_ptr` (not `unique_ptr`): the component pool moves elements by value when it
 * grows, and a copyable member keeps that path trivially valid.
 */
struct FUIWidget
{
	std::shared_ptr<UI::FUIView> View;
};

/**
 * GameWorld UI system -- a WORLD SYSTEM, installed into FGameWorld as a peer layer (via
 * the sub-landlord collector). It owns the game-side UI: every `FUIWidget` entity owns a
 * persistent `UI::FUIView` registered in the UI view registry. Each Update it re-declares
 * every widget's tree from its component -- an EXCLUSIVE write (`Edit()`, the tree's
 * `std::shared_mutex`) -- and then drains the events the translation thread queued while
 * the render side walked the tree under a SHARED read. So the cross-thread contract is
 * exclusive-write / shared-read, and neither side holds the tree across the boundary.
 *
 * No ImGui here: the game declares trees through the UI plugin's builder API, and the
 * translation layer (owned by the render feature) maps them to ImGui.
 */
class MAHO_UISYSTEM_API FUISystem : public FLayer<IOnInstalled, IProcessInput, IUpdate, IPreUnInstall>
{
	MAHO_DECLARE_LAYER(FUISystem);

public:
	void OnInstalled(FGameWorld& World) override;
	void ProcessInput(FGameWorld& World) override;
	void Update(FGameWorld& World) override;
	void PreUnInstall(FGameWorld& World) override;

private:
	/** Create (once) the demo widget's entity + component + persistent view, bind it to
	 *  the game render context and register it. Returns nullptr while the UI registry or
	 *  the game context is not up yet -- the caller retries next frame (init order
	 *  between the game world and the render features is not fixed). */
	UI::FUIView* EnsureDemoView(FGameWorld& World);

	/** Re-declare the demo tree (called from inside a live `Edit()` scope). */
	void BuildDemoTree(UI::FUIBuilder& Root);

	FEntity DemoWidget;
};

/** Global accessor to the UI system (cross-DLL, mirrors Resource::GetResourceSystem()).
 *  nullptr until the system is installed by FGameWorld. */
MAHO_UISYSTEM_API FUISystem* GetUISystem();

} // namespace GameWorld
} // namespace Maho
