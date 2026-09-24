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
class MAHO_UISYSTEM_API FUISystem : public FFrameExtension, public IPipeline<IOnInstalled, IProcessInput, IUpdate, IPreUnInstall>
{
	MAHO_DECLARE_FRAME(FUISystem);

public:
	void OnInstalled(FGameWorld& World, FGameWorldContext& Frame) override;
	void ProcessInput(FGameWorld& World, FGameWorldContext& Frame) override;
	void Update(FGameWorld& World, FGameWorldContext& Frame) override;
	void PreUnInstall(FGameWorld& World, FGameWorldContext& Frame) override;

public:
	static UI::FUIName GameRenderScope();

private:
	/** Create (once) the demo widget's entity + component + persistent view, bind it to
	 *  the game render context and register it. Returns nullptr while the UI registry or
	 *  the game context is not up yet -- the caller retries next frame (init order
	 *  between the game world and the render features is not fixed). */
	UI::FUIView* EnsureDemoView(FGameWorld& World);

	/** Re-declare the demo tree (called from inside a live `Edit()` scope). */
	void BuildDemoTree(UI::FUIBuilder& Root);

	FEntity DemoWidget;

	/** 本系统也是视图的所有者（与组件里的 `shared_ptr` 共享所有权）。注销必须只靠它：
	 *  `FGameWorld::Shutdown` 为了防跨模块析构，在跑系统的 `IPreUnInstall` 之前就清空了组件池
	 *  （GameWorld.cpp:126），所以那时从组件里取视图必然取不到 —— 注销会静默丢失，视图残留在
	 *  注册表里直到它关闭（实测：`注册表关闭时仍有 1 个视图在册`）。持有 `shared_ptr` 还保证
	 *  对象活到注销完成（池被清后引用计数仍 >= 1）。 */
	std::shared_ptr<UI::FUIView> DemoView;

	/** 平滑后的帧率（`Update` 每帧从 `World.GetDeltaSeconds()` 采样）。仅用于演示读数。 */
	float SmoothedFps = 0.f;

	/** 距上次把帧率写进日志过了几帧（每 60 帧一行，见 `Update`）。 */
	std::uint32_t FpsLogFrames = 0;
};

/** Global accessor to the UI system (cross-DLL, mirrors Resource::GetResourceSystem()).
 *  nullptr until the system is installed by FGameWorld. */
MAHO_UISYSTEM_API FUISystem* GetUISystem();

} // namespace GameWorld
} // namespace Maho
