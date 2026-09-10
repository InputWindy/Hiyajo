#include "EditorViewport.h"

#include <UIViewRegistry.h>
#include <Widgets/FUIImage.h>

namespace Maho
{

namespace
{
// -- 稳定节点 Id（同级唯一即可；跨级可重名，事件路由走路径）-------------------
constexpr const char* kIdImage = "EditorViewport.Image";

/** 取（必要时新建）子节点，并报告它是否是本帧新建的 —— 新建才需要播种默认配置
 *  （资源引用 / 缩放模式；逐帧重来会覆盖用户在别处改过的东西）。 */
template <typename T>
T& Ensure(UI::FUIBuilder& Parent, UI::FUIName Id, bool& bOutCreated)
{
	bOutCreated = (Parent.FindChild(Id) == nullptr);
	return Parent.AddItem<T>(Id);
}
} // namespace

UI::FUIView* FEditorViewport::EnsureView(FExampleEditor& Editor)
{
	if (View != nullptr)
	{
		return View.get();
	}

	// 注册表由 UI 插件发布；插件未起来（或已关）时返回 nullptr，下一帧再试。
	UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry();
	if (Registry == nullptr)
	{
		return nullptr;
	}

	std::unique_ptr<UI::FUIView> NewView = std::make_unique<UI::FUIView>(UI::FUIName("EditorViewport"));
	// 外壳开窗由宿主通用循环做（含它自己的 dockspace id）；标题即旧窗口名。
	NewView->SetWindowShell(true, "Viewport", UI::FUIVector2{}, UI::FUIVector2{},
							UI::EUIShellFlags::NoCollapse | UI::EUIShellFlags::NoMove);
	NewView->SetRenderContext(Editor.GetUIRenderContext());
	Registry->RegisterView(*NewView);
	View = std::move(NewView);
	return View.get();
}

void FEditorViewport::Shutdown(FExampleEditor& Editor)
{
	(void)Editor;

	if (View == nullptr)
	{
		return;
	}
	if (UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry())
	{
		Registry->UnregisterView(*View);
	}
	View.reset();
}

void FEditorViewport::Update(FExampleEditor& Editor)
{
	UI::FUIView* PanelView = EnsureView(Editor);
	if (PanelView == nullptr)
	{
		return;
	}

	UI::FUIRect ImageRect{};
	{
		UI::FUIEditScope Scope = PanelView->Edit();
		UI::FUIBuilder& Root = Scope.GetRoot();
		Root.Layout().SetDirection(UI::EUIDirection::Column);
		// 取景区要贴边铺满：面板按视图显示区取满，根容器的主题内边距在此清掉。
		Root.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Fill());
		Root.Layout().SetPadding(UI::FMargin{ 0.f, 0.f });

		bool bNew = false;
		UI::FUIImage& Image = Ensure<UI::FUIImage>(Root, UI::FUIName(kIdImage), bNew);
		Image.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Fill());
		if (bNew)
		{
			// 取景区显示的是"本帧将要呈现的画面"：按名字引用宿主 present target，
			// 由宿主注入的解析器把名字映回原生句柄（组件不认识 ImTextureID）。
			// Stretch = 铺满本节点矩形（等价旧 `ImGui::Image(id, Sz)`）。
			Image.SetTexture(FExampleEditor::PresentTargetName());
			Image.SetScaleMode(UI::EUIScaleMode::Stretch);
		}
		// 翻译期后端把本帧摆位写回节点：这里读到的是上一帧的屏幕矩形（与旧版同序）。
		ImageRect = Image.GetScreenRect();
	}

	if (ImageRect.W > 0.f && ImageRect.H > 0.f)
	{
		// 发布面板矩形（编辑器显示空间）供宿主 pass0 的 IEditorInput 重定游戏光标到它。
		Editor.ReportViewportRect(ImageRect.X, ImageRect.Y, ImageRect.W, ImageRect.H);
	}
}

} // namespace Maho

extern "C" MAHO_EDITORVIEWPORT_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FEditorViewport::CreateLayer();
}
