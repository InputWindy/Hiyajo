#include "EditorTheme.h"

#include <ExampleEditorTheme.h>

#include <UIViewRegistry.h>
#include <Widgets/FUIBox.h>
#include <Widgets/FUIButton.h>
#include <Widgets/FUICheckbox.h>
#include <Widgets/FUICollapsingHeader.h>
#include <Widgets/FUIColorEdit.h>
#include <Widgets/FUIDragFloat.h>
#include <Widgets/FUIInputText.h>
#include <Widgets/FUIPanel.h>
#include <Widgets/FUISeparator.h>
#include <Widgets/FUIText.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace Maho
{

namespace
{
// -- 稳定节点 Id（同级唯一即可；跨级可重名，事件路由走路径）-------------------
constexpr const char* kIdRowTop      = "EditorTheme.TopRow";
constexpr const char* kIdLiveApply   = "EditorTheme.LiveApply";
constexpr const char* kIdColorInfo   = "EditorTheme.ColorInfo";
constexpr const char* kIdSepTop      = "EditorTheme.Sep.Top";
constexpr const char* kIdBody        = "EditorTheme.Body";
constexpr const char* kIdSepStyle    = "EditorTheme.Sep.Style";
constexpr const char* kIdStyleTitle  = "EditorTheme.StyleTitle";
constexpr const char* kIdSepPath     = "EditorTheme.Sep.Path";
constexpr const char* kIdPathRow     = "EditorTheme.PathRow";
constexpr const char* kIdPath        = "EditorTheme.Path";
constexpr const char* kIdPathLabel   = "EditorTheme.PathLabel";
constexpr const char* kIdOpsRow      = "EditorTheme.OpsRow";
constexpr const char* kIdBtnApply    = "EditorTheme.Button.Apply";
constexpr const char* kIdBtnReset    = "EditorTheme.Button.Reset";
constexpr const char* kIdBtnSave     = "EditorTheme.Button.Save";
constexpr const char* kIdBtnLoad     = "EditorTheme.Button.Load";
constexpr const char* kIdStatus      = "EditorTheme.Status";

/** 逐槽位的 Id：颜色 / 样式 / 分组各一串，序号在各自块内单调。 */
UI::FUIName MakeId(const char* Prefix, int Index)
{
	return UI::FUIName(std::string(Prefix) + std::to_string(Index));
}

/** 取（必要时新建）子节点，并报告它是否是本帧新建的 —— 新建才需要播种默认配置
 *  （`DefaultOpen` / 输入框初值 / 事件订阅；逐帧重来会累积订阅）。 */
template <typename T>
T& Ensure(UI::FUIBuilder& Parent, UI::FUIName Id, bool& bOutCreated)
{
	bOutCreated = (Parent.FindChild(Id) == nullptr);
	return Parent.AddItem<T>(Id);
}
} // namespace

void FEditorTheme::ReloadDefaults()
{
	const int CCount = GetEditorThemeColorCount();
	const int SCount = GetEditorThemeStyleCount();
	Colors.assign(static_cast<std::size_t>(CCount) * 4, 0.0f);
	Styles.assign(static_cast<std::size_t>(SCount) * 2, 0.0f);
	for (int i = 0; i < CCount; ++i)
	{
		GetEditorThemeColorDefault(i, &Colors[static_cast<std::size_t>(i) * 4]);
	}
	for (int i = 0; i < SCount; ++i)
	{
		GetEditorThemeStyleDefault(i, &Styles[static_cast<std::size_t>(i) * 2]);
	}
}

void FEditorTheme::Init(FExampleEditor& Editor)
{
	(void)Editor;

	std::strncpy(PathBuffer, GetEditorThemeDefaultPath(), sizeof(PathBuffer) - 1);
	PathBuffer[sizeof(PathBuffer) - 1] = '\0';

	ReloadDefaults();
	// Start from a persisted theme when one exists, otherwise stay on defaults.
	if (LoadEditorTheme(PathBuffer, Colors.data(), Styles.data()))
	{
		ApplyEditorTheme(Colors.data());
		ApplyEditorThemeStyles(Styles.data());
	}
}

UI::FUIView* FEditorTheme::EnsureView(FExampleEditor& Editor)
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

	std::unique_ptr<UI::FUIView> NewView = std::make_unique<UI::FUIView>(UI::FUIName("EditorTheme"));
	// 外壳开窗由宿主通用循环做（含它自己的 dockspace id）；标题即旧窗口名。
	// 关闭框归宿主所有：旧版也没有绑定关闭语义，故这里不订阅 `WindowClosed`。
	NewView->SetWindowShell(true, "EditorTheme", UI::FUIVector2{}, UI::FUIVector2{}, UI::EUIShellFlags::NoCollapse);
	NewView->SetRenderContext(Editor.GetUIRenderContext());
	Registry->RegisterView(*NewView);
	View = std::move(NewView);
	return View.get();
}

void FEditorTheme::Shutdown(FExampleEditor& Editor)
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

void FEditorTheme::Update(FExampleEditor& Editor)
{
	UI::FUIView* PanelView = EnsureView(Editor);
	if (PanelView == nullptr)
	{
		return;
	}

	const int CCount = GetEditorThemeColorCount();
	const int SCount = GetEditorThemeStyleCount();
	Colors.resize(static_cast<std::size_t>(CCount) * 4, 0.0f);
	Styles.resize(static_cast<std::size_t>(SCount) * 2, 0.0f);

	// -- 值回读 ------------------------------------------------------------
	// 翻译期后端原地改写节点的值（拖拽/取色中的真值在节点上），故先把它们收回缓冲，
	// 再按缓冲声明 —— 用户编辑因此跨帧存活。Reset/Load 刚换过缓冲的那一帧除外：
	// 此时缓冲才是真值，回读会把节点的陈旧值压回去（bBuffersAuthoritative）。
	bool bDirty = false;
	if (!bBuffersAuthoritative)
	{
		for (int i = 0; i < CCount; ++i)
		{
			const auto* Node = dynamic_cast<const UI::FUIColorEdit*>(PanelView->Find(MakeId("EditorTheme.Color.", i)));
			if (Node == nullptr)
			{
				continue;
			}
			const UI::FUIColor Live = Node->GetValue();
			const float Live4[4] = { Live.R, Live.G, Live.B, Live.A };
			float* Slot = &Colors[static_cast<std::size_t>(i) * 4];
			for (int k = 0; k < 4; ++k)
			{
				if (Live4[k] != Slot[k])
				{
					bDirty = true;
				}
				Slot[k] = Live4[k];
			}
		}
		for (int i = 0; i < SCount; ++i)
		{
			const int Arity = GetEditorThemeStyleArity(i);
			const auto* Node = dynamic_cast<const UI::FUIDragFloat*>(PanelView->Find(MakeId("EditorTheme.Style.", i)));
			if (Node == nullptr || Node->GetComponents() != Arity)
			{
				continue;
			}
			const float* Live = Node->GetValues();
			float* Slot = &Styles[static_cast<std::size_t>(i) * 2];
			for (int k = 0; k < Arity; ++k)
			{
				if (Live[k] != Slot[k])
				{
					bDirty = true;
				}
				Slot[k] = Live[k];
			}
		}
	}
	bBuffersAuthoritative = false;

	// 勾选态由控件自己翻转（复制语义），必须回读。
	if (const auto* Live = dynamic_cast<const UI::FUICheckbox*>(PanelView->Find(UI::FUIName(kIdLiveApply))))
	{
		LiveApply = Live->IsChecked();
	}
	// 路径框的值同样是用户输入：回读进缓冲（Save/Load 用它）。
	if (const auto* PathNode = dynamic_cast<const UI::FUIInputText*>(PanelView->Find(UI::FUIName(kIdPath))))
	{
		std::strncpy(PathBuffer, PathNode->GetValue().c_str(), sizeof(PathBuffer) - 1);
		PathBuffer[sizeof(PathBuffer) - 1] = '\0';
	}

	// -- 声明期：只改本视图的树 -------------------------------------------
	UI::FUIEditScope Scope = PanelView->Edit();
	UI::FUIBuilder& Root = Scope.GetRoot();
	Root.Layout().SetDirection(UI::EUIDirection::Column);
	Root.Layout().SetSpacing(6.f);

	char Info[64];
	std::snprintf(Info, sizeof(Info), "%d colors / %d styles", CCount, SCount);

	// 旧：Checkbox("Live apply") + SameLine() + TextColored(0.62,0.64,0.70)
	bool bNew = false;
	{
		UI::FUIBox& TopRow = Ensure<UI::FUIBox>(Root, UI::FUIName(kIdRowTop), bNew);
		TopRow.Layout().SetDirection(UI::EUIDirection::Row);
		TopRow.Layout().SetSpacing(8.f);
		TopRow.Layout().VerticalAlign = UI::EUIAlign::Center;
		TopRow.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());

		UI::FUICheckbox& LiveBox = Ensure<UI::FUICheckbox>(TopRow, UI::FUIName(kIdLiveApply), bNew);
		LiveBox.SetLabel("Live apply");
		LiveBox.SetChecked(LiveApply);

		UI::FUIText& InfoText = Ensure<UI::FUIText>(TopRow, UI::FUIName(kIdColorInfo), bNew);
		InfoText.SetText(Info);
		InfoText.Style()[UI::EUIState::Normal].Text = UI::FUIColor{ 0.62f, 0.64f, 0.70f, 1.0f };
	}

	Root.AddItem<UI::FUISeparator>(UI::FUIName(kIdSepTop));

	// -- 滚动主体（旧 BeginChild("theme", ImVec2(0, -96), true)）-----------
	// 高度 Fill：同级的其余项都是内容高，主体自然拿到"剩余空间"，不必再写死 -96。
	UI::FUIPanel& Body = Ensure<UI::FUIPanel>(Root, UI::FUIName(kIdBody), bNew);
	Body.SetChrome(UI::EUIPanelChrome::Full);
	Body.SetScrollable(true);
	Body.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Fill());
	Body.Layout().SetSpacing(2.f);

	// 颜色：按组逐段渲染，一个 ColorEdit4 一个槽位。
	int Group = 0;
	for (int i = 0; i < CCount;)
	{
		const char* GroupName = GetEditorThemeColorGroup(i);
		UI::FUICollapsingHeader& Header =
			Ensure<UI::FUICollapsingHeader>(Body, MakeId("EditorTheme.ColorGroup.", Group), bNew);
		Header.SetLabel(GroupName);
		if (bNew)
		{
			Header.SetDefaultOpen(true);
		}

		int j = i;
		while (j < CCount && std::strcmp(GetEditorThemeColorGroup(j), GroupName) == 0)
		{
			UI::FUIColorEdit& Edit = Header.AddItem<UI::FUIColorEdit>(MakeId("EditorTheme.Color.", j));
			Edit.SetLabel(GetEditorThemeColorName(j));
			Edit.SetValue(UI::FUIColor{ Colors[static_cast<std::size_t>(j) * 4 + 0],
										Colors[static_cast<std::size_t>(j) * 4 + 1],
										Colors[static_cast<std::size_t>(j) * 4 + 2],
										Colors[static_cast<std::size_t>(j) * 4 + 3] });
			++j;
		}
		i = j;
		++Group;
	}

	Body.AddItem<UI::FUISeparator>(UI::FUIName(kIdSepStyle));

	UI::FUIText& StyleTitle = Body.AddItem<UI::FUIText>(UI::FUIName(kIdStyleTitle));
	StyleTitle.SetText("Style (rounding / padding / spacing)");

	// 样式：按组逐段渲染，一个 DragFloat(N) 一个槽位（步长 0.25 与旧版一致）。
	Group = 0;
	for (int i = 0; i < SCount;)
	{
		const char* GroupName = GetEditorThemeStyleGroup(i);
		UI::FUICollapsingHeader& Header =
			Ensure<UI::FUICollapsingHeader>(Body, MakeId("EditorTheme.StyleGroup.", Group), bNew);
		Header.SetLabel(GroupName);
		if (bNew)
		{
			Header.SetDefaultOpen(true);
		}

		int j = i;
		while (j < SCount && std::strcmp(GetEditorThemeStyleGroup(j), GroupName) == 0)
		{
			UI::FUIDragFloat& Drag = Header.AddItem<UI::FUIDragFloat>(MakeId("EditorTheme.Style.", j));
			Drag.SetLabel(GetEditorThemeStyleName(j));
			Drag.SetComponents(GetEditorThemeStyleArity(j));   // 先定分量，再给值
			Drag.SetSpeed(0.25f);
			Drag.SetValues(&Styles[static_cast<std::size_t>(j) * 2]);
			++j;
		}
		i = j;
		++Group;
	}

	Root.AddItem<UI::FUISeparator>(UI::FUIName(kIdSepPath));

	// -- 路径 + 操作按钮 ---------------------------------------------------
	// 旧 InputText("Path", ...) 的标签画在输入框右侧：这里用同一行的兄弟文本还原。
	UI::FUIBox& PathRow = Ensure<UI::FUIBox>(Root, UI::FUIName(kIdPathRow), bNew);
	PathRow.Layout().SetDirection(UI::EUIDirection::Row);
	PathRow.Layout().SetSpacing(8.f);
	PathRow.Layout().VerticalAlign = UI::EUIAlign::Center;
	PathRow.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());

	UI::FUIInputText& PathInput = Ensure<UI::FUIInputText>(PathRow, UI::FUIName(kIdPath), bNew);
	PathInput.SetMaxLength(sizeof(PathBuffer) - 1);   // 旧 IM_ARRAYSIZE(PathBuffer)
	if (bNew)
	{
		PathInput.SetValue(PathBuffer);   // 之后以用户输入为准（不再逐帧压回）
	}
	PathInput.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());

	UI::FUIText& PathLabel = Ensure<UI::FUIText>(PathRow, UI::FUIName(kIdPathLabel), bNew);
	PathLabel.SetText("Path");

	UI::FUIBox& OpsRow = Ensure<UI::FUIBox>(Root, UI::FUIName(kIdOpsRow), bNew);
	OpsRow.Layout().SetDirection(UI::EUIDirection::Row);
	OpsRow.Layout().SetSpacing(8.f);
	OpsRow.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());

	// 事件订阅随节点创建只绑一次 —— 节点长期复用，逐帧再绑会累积订阅。
	UI::FUIButton& ApplyBtn = Ensure<UI::FUIButton>(OpsRow, UI::FUIName(kIdBtnApply), bNew);
	ApplyBtn.SetLabel("Apply");
	if (bNew)
	{
		ApplyBtn.OnClick(
			[this](UI::FUIBuilder&)
			{
				ApplyEditorTheme(Colors.data());
				ApplyEditorThemeStyles(Styles.data());
				std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Applied.");
			});
	}

	UI::FUIButton& ResetBtn = Ensure<UI::FUIButton>(OpsRow, UI::FUIName(kIdBtnReset), bNew);
	ResetBtn.SetLabel("Reset");
	if (bNew)
	{
		ResetBtn.OnClick(
			[this](UI::FUIBuilder&)
			{
				ReloadDefaults();
				ApplyEditorTheme(Colors.data());
				ApplyEditorThemeStyles(Styles.data());
				bBuffersAuthoritative = true;
				std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Reset to defaults.");
			});
	}

	UI::FUIButton& SaveBtn = Ensure<UI::FUIButton>(OpsRow, UI::FUIName(kIdBtnSave), bNew);
	SaveBtn.SetLabel("Save");
	if (bNew)
	{
		SaveBtn.OnClick(
			[this](UI::FUIBuilder&)
			{
				if (SaveEditorTheme(PathBuffer, Colors.data(), Styles.data()))
				{
					std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Saved: %s", PathBuffer);
				}
				else
				{
					std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Save failed.");
				}
			});
	}

	UI::FUIButton& LoadBtn = Ensure<UI::FUIButton>(OpsRow, UI::FUIName(kIdBtnLoad), bNew);
	LoadBtn.SetLabel("Load");
	if (bNew)
	{
		LoadBtn.OnClick(
			[this](UI::FUIBuilder&)
			{
				if (LoadEditorTheme(PathBuffer, Colors.data(), Styles.data()))
				{
					ApplyEditorTheme(Colors.data());
					ApplyEditorThemeStyles(Styles.data());
					bBuffersAuthoritative = true;
					std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Loaded: %s", PathBuffer);
				}
				else
				{
					std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Load failed.");
				}
			});
	}

	UI::FUIText& Status = Root.AddItem<UI::FUIText>(UI::FUIName(kIdStatus));
	Status.SetText(StatusBuffer);
	Status.SetVisible(StatusBuffer[0] != '\0');   // 旧：仅非空时输出（隐藏项不占布局）

	// 实时预览：本帧回读与缓冲不一致 = 用户刚动过控件 → 推到实盘样式。
	if (bDirty && LiveApply)
	{
		ApplyEditorTheme(Colors.data());
		ApplyEditorThemeStyles(Styles.data());
	}
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_EDITORTHEME_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FEditorTheme::CreateLayer();
}
