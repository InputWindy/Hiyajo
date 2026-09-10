#include "UISystem.h"

#include <UIViewRegistry.h>
#include <Widgets/FUIText.h>

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

	// 注册表与游戏上下文都由 UI 插件/渲染特性发布；任一未就位就下一帧再试。
	UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry();
	if (Registry == nullptr)
	{
		return nullptr;
	}
	void* Context = UI::GetUIGameRenderContext();
	if (Context == nullptr)
	{
		return nullptr;
	}

	std::shared_ptr<UI::FUIView> View =
		std::make_shared<UI::FUIView>(UI::FUIName("GameUI"), UI::EUIOwnership::CrossThread);
	// 外壳开窗：旧演示是 "Game UI" 标题 + NoResize，位置/尺寸按显示区比例给
	// （位置只在首次生效，之后可由标题栏拖动 —— 与旧 `FUIWidget` 锚点语义一致）。
	View->SetWindowShell(true, "Game UI", {}, {}, UI::EUIShellFlags::NoResize);
	View->SetShellFractions(UI::FUIVector2{ 0.05f, 0.05f }, UI::FUIVector2{ 0.30f, 0.25f });
	View->SetRenderContext(Context);
	Registry->RegisterView(*View);

	FEntity E = World.CreateEntity();
	FUIWidget Component;
	Component.View = std::move(View);
	World.AddComponent<FUIWidget>(E, std::move(Component));
	DemoWidget = E;

	return World.GetComponent<FUIWidget>(E)->View.get();
}

void FUISystem::BuildDemoTree(UI::FUIBuilder& Root)
{
	Root.Layout().SetDirection(UI::EUIDirection::Column);
	Root.Layout().SetSpacing(4.f);

	// 节点 Id 稳定：同 Id 同类型重声明 = 复用（运行期状态跨帧存活）。
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
	if (UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry())
	{
		if (DemoWidget.IsValid())
		{
			if (const FUIWidget* Widget = World.GetComponent<FUIWidget>(DemoWidget))
			{
				if (Widget->View != nullptr)
				{
					Registry->UnregisterView(*Widget->View);
				}
			}
		}
	}
	if (DemoWidget.IsValid())
	{
		World.DestroyEntity(DemoWidget);
		DemoWidget = FEntity{};
	}
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
