#include "UISystem.h"

#include <ConsoleVariable.h>
#include <UIViewRegistry.h>
#include <Widgets/FUIText.h>

#include <Trace.h>
#include <cstdio>

namespace Maho
{
namespace GameWorld
{

/** `r.stat.hud` -- the top-left frame-rate HUD. A development stat, so NOT `ECVarFlags::Shipping`:
 *  a Shipping build does not register it and the value falls back to the local default below --
 *  which is 1 in Debug/Release (the HUD is on unless somebody switches it off) and 0 in Shipping
 *  (a release ships without a debug overlay, and with no CVar to switch it on). */
#if MAHO_BUILD_SHIPPING
static constexpr int kStatHudDefault = 0;
#else
static constexpr int kStatHudDefault = 1;
#endif

static ConsoleVariable::TAutoConsoleVariable<int> GCVarStatHud(
	"r.stat.hud", kStatHudDefault, "1 = draw the top-left frame-rate HUD, 0 = hide it");

// Global accessor target (cross-DLL, mirrors Resource::GResourceSystem). Set at
// OnInstalled, cleared at PreUnInstall.
FUISystem* GUISystem = nullptr;

void FUISystem::OnInstalled(FGameWorld& World, FGameWorldContext& Frame)
{
	MAHO_TRACE_STAGE(IOnInstalled, "UI system install", "publish the system to the world");
	GUISystem = this;
}

void FUISystem::ProcessInput(FGameWorld&, FGameWorldContext&)
{
	MAHO_TRACE_STAGE(IProcessInput, "UI system input", "the tree is fed by the translation layer");
	// Input hook -- the world's IProcessInput stage. UI input is delivered to the tree by
	// the translation layer (the render side owns the context), so there is nothing to
	// poll here; the stage stays as a declared-but-empty capability.
}

UI::FUIView* FUISystem::EnsureDemoView(FGameWorld& World)
{
	if (DemoWidget.IsValid())
	{
		FUIWidget* Widget = World.GetComponent<FUIWidget>(DemoWidget);
		if (Widget != nullptr && Widget->View != nullptr)
		{
			return Widget->View.get();
		}
	}

	// 注册表由 UI 层发布（层 Init 后非空）；未就位就下一帧再试。
	UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry();
	if (Registry == nullptr)
	{
		return nullptr;
	}

	std::shared_ptr<UI::FUIView> View =
		std::make_shared<UI::FUIView>(UI::FUIName("GameUI"), UI::EUIOwnership::CrossThread);
	// 外壳（窗口）由 `Update` 按 `r.stat.hud` 每帧声明 —— 外壳开关是"结构声明"，必须在
	// `Edit()` 区间内改（见 UIView.h）。此处只给初始值，免得视图第一帧没有外壳可翻。
	View->SetWindowShell(true, "FPS", {}, {}, UI::EUIShellFlags::NoResize);
	// 声明本视图归游戏的翻译循环：与 `FUIFeature` 的翻译入口是同一个名字（同一个字面量 =
	// 同一个作用域）。本系统因此不必知道任何 ImGui 上下文 —— 游戏侧不感知渲染侧。
	View->SetRenderScope(FUISystem::GameRenderScope());
	Registry->RegisterView(*View);
	DemoView = View;   // 本系统自持一份（见 UISystem.h：注销时组件池可能已被清空）

	FEntity E = World.CreateEntity();
	FUIWidget Component;
	Component.View = std::move(View);
	World.AddComponent<FUIWidget>(E, std::move(Component));
	DemoWidget = E;

	return DemoView.get();
}

void FUISystem::BuildDemoTree(UI::FUIBuilder& Root, bool bVisible)
{
	// 节点 Id 稳定：同 Id 同类型重声明 = 复用（运行期状态跨帧存活）。
	char Line[64] = {};
	std::snprintf(Line, sizeof(Line), "FPS %.1f   (%.2f ms)",
		static_cast<double>(SmoothedFps),
		SmoothedFps > 0.f ? 1000.0 / static_cast<double>(SmoothedFps) : 0.0);
	// 关闭 = 【隐藏】而不是"停止声明"：`AddItem` 只按 Id 复用、从不剪枝（"未声明者移除"是 `[]`
	// 块语义的事），少声明一次节点并不会让它从树里消失 —— 那正是"关了 HUD 还剩一行字"。
	Root.AddItem<UI::FUIText>(UI::FUIName("GameUI.Fps")).SetText(Line).SetVisible(bVisible);
}

void FUISystem::Update(FGameWorld& World, FGameWorldContext& Frame)
{
	MAHO_TRACE_STAGE(IUpdate, "UI system update", "drain the events and re-declare the demo tree");
	UI::FUIView* View = EnsureDemoView(World);
	if (View == nullptr)
	{
		return;   // 注册表/上下文未就位，下一帧再试
	}

	// 帧率读数：世界自己的帧时（固定步进用的同一份 delta）。指数平滑，否则读数会跳。
	const float Delta = World.GetDeltaSeconds();
	if (Delta > 0.f)
	{
		const float Instant = 1.f / Delta;
		SmoothedFps = (SmoothedFps <= 0.f) ? Instant : (SmoothedFps * 0.9f + Instant * 0.1f);
	}

	// 交互事件先回传：翻译线程只入队，真正的回调在所有者线程（此处）执行，回调内可再次 Edit()。
	View->DrainEvents();

	// 声明期：独占写（std::shared_mutex）。翻译线程持共享读时本段会等它翻完这一帧，反之亦然。
	// `r.stat.hud` 只决定两件"声明"：外壳开不开、那行字可不可见。位置/尺寸用【非零】比例锚点 ——
	// 翻译层只在 `PosFraction > 0` 时才采纳比例（`{0,0}` 会掉进 ImGui 的默认窗口落位，那正是
	// "HUD 看着不在左上角"的原因）。
	const bool bHud = GCVarStatHud.GetValue() != 0;
	{
		UI::FUIEditScope Scope = View->Edit();
		if (bHud)
		{
			// 左上角 HUD：无标题栏 / 不可拖动缩放 / 无滚动条 / 不写 ini（位置归代码管）。
			View->SetWindowShell(true, "FPS", {}, {},
				UI::EUIShellFlags::NoTitleBar | UI::EUIShellFlags::NoMove | UI::EUIShellFlags::NoResize |
				UI::EUIShellFlags::NoScrollbar | UI::EUIShellFlags::NoSavedSettings);
			View->SetShellFractions(UI::FUIVector2{ 0.008f, 0.012f }, UI::FUIVector2{ 0.16f, 0.055f });
		}
		else
		{
			// 关掉：外壳收掉（无壳时翻译层只铺一层透明叠加窗口），那行字也置为不可见。
			View->SetWindowShell(false, "FPS", {}, {}, UI::EUIShellFlags::None);
		}
		BuildDemoTree(Scope.GetRoot(), bHud);
	}
}

void FUISystem::PreUnInstall(FGameWorld& World, FGameWorldContext& Frame)
{
	MAHO_TRACE_STAGE(IPreUnInstall, "UI system teardown", "unregister the view and drop the widget");
	// 关表先于释放树：注册表只持裸指针，视图必须先注销再被销毁。
	// 顺序由 FGameWorld 声明（它才是驱动本阶段的层）：`BlockOn("FUIViewRegistry", IShutdown)`
	// 让注册表的 IShutdown 排在本层 IShutdown（→ 本系统的 PreUnInstall）之后。此处判空只是
	// 防御左值（注册表未安装时本系统也拿不到它）。
	//
	// 用 DemoView 而不是从组件取：FGameWorld::Shutdown 在驱动本阶段之前已经清空了组件池，
	// 那时 GetComponent 必然返回 null，视图就会残留在注册表里（详见 UISystem.h 的 DemoView）。
	if (UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry())
	{
		if (DemoView != nullptr)
		{
			Registry->UnregisterView(*DemoView);
		}
	}
	DemoView.reset();
	if (DemoWidget.IsValid())
	{
		World.DestroyEntity(DemoWidget);
		DemoWidget = FEntity{};
	}
	GUISystem = nullptr;
}

UI::FUIName FUISystem::GameRenderScope()
{
	// 名字只解析一次：名字池的 intern 带锁，不值得每次比较都做。
	static const UI::FUIName Scope("UI.Scope.Game");
	return Scope;
}

FUISystem* GetUISystem()
{
	return GUISystem;
}

} // namespace GameWorld
} // namespace Maho

// The C export FGameWorld looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_UISYSTEM_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::GameWorld::FUISystem::CreateFrame();
}
