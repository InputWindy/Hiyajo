#include "UISystem.h"

#include <UIViewRegistry.h>
#include <Widgets/FUIText.h>

#include <cstdio>

namespace Maho
{
namespace GameWorld
{

// Global accessor target (cross-DLL, mirrors Resource::GResourceSystem). Set at
// OnInstalled, cleared at PreUnInstall.
FUISystem* GUISystem = nullptr;

void FUISystem::OnInstalled(FGameWorld& World)
{
	GUISystem = this;
}

void FUISystem::ProcessInput(FGameWorld&)
{
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
	// 外壳开窗：旧演示是 "Game UI" 标题 + NoResize，位置/尺寸按显示区比例给
	// （位置只在首次生效，之后可由标题栏拖动 —— 与旧 `FUIWidget` 锚点语义一致）。
	View->SetWindowShell(true, "Game UI", {}, {}, UI::EUIShellFlags::NoResize);
	View->SetShellFractions(UI::FUIVector2{ 0.05f, 0.05f }, UI::FUIVector2{ 0.30f, 0.25f });
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

void FUISystem::BuildDemoTree(UI::FUIBuilder& Root)
{
	Root.Layout().SetDirection(UI::EUIDirection::Column);
	Root.Layout().SetSpacing(4.f);

	// 节点 Id 稳定：同 Id 同类型重声明 = 复用（运行期状态跨帧存活）。
	{
		// 帧率读数：FPS + 帧时间（ms）。没有 profiler 时，这一行就是"是不是帧率低"的答案。
		char Line[64];
		std::snprintf(Line, sizeof(Line), "FPS %.1f   (%.2f ms)",
			static_cast<double>(SmoothedFps),
			SmoothedFps > 0.f ? 1000.0 / static_cast<double>(SmoothedFps) : 0.0);
		Root.AddItem<UI::FUIText>(UI::FUIName("GameUI.Fps")).SetText(Line);
	}
	Root.AddItem<UI::FUIText>(UI::FUIName("GameUI.Label")).SetText("Game UI placeholder");

	Root.AddItem<UI::FUIText>(UI::FUIName("GameUI.Hint"))
		.SetText("Drag this window by its title bar.")
		.SetWrap(true);
}

void FUISystem::Update(FGameWorld& World)
{
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
	{
		UI::FUIEditScope Scope = View->Edit();
		BuildDemoTree(Scope.GetRoot());
	}
}

void FUISystem::PreUnInstall(FGameWorld& World)
{
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
