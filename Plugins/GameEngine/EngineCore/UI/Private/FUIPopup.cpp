// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUIPopup.h>

#include "UILayoutEngine.h"
#include "WidgetStyle.h"

namespace Maho { namespace UI {

namespace
{
/** 弹层内容测量的宽度上限（弹窗自体自适应尺寸）。 */
constexpr float kPopupMeasureWidth = 320.f;
} // namespace

FUIPopup::FUIPopup(FUIName InId)
	: FUIBuilder(InId)
{
}

FUIPopup::~FUIPopup() = default;

std::string_view FUIPopup::TypeName() const
{
	return "FUIPopup";
}

const FUIStyle& FUIPopup::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		// 底与描边按声明给：弹层窗口的底/边框/圆角/边框宽由后端压成这里声明的值（见
		// `FImGuiTranslator::PushWindowStyle`）—— 字段色那套（近黑底 + 灰描边）与输入框同源，
		// "弹层内里和 cvar 输入框一样"因此是声明说了算，不再依赖后端去读 ImGui 的 `ImGuiCol_FrameBg`。
		S[EUIState::Normal].Fill      = Th.FieldFill;
		S[EUIState::Normal].Stroke    = Th.FieldStroke;
		S[EUIState::Normal].Text      = Th.Text;
		S[EUIState::Normal].FontSize  = Th.FontSize;
		S[EUIState::Normal].Radius    = Th.Radius;
		S[EUIState::Normal].StrokeWidth = Th.StrokeWidth;
		S[EUIState::Normal].Padding   = FMargin(10.f, 8.f);

		S[EUIState::Hovered].Text = Th.Text;
		S[EUIState::Pressed].Text = Th.Text;
		S[EUIState::Selected].Text = Th.Text;
		S[EUIState::Disabled].Text = Th.TextDisabled;
	});
}

FUIVector2 FUIPopup::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	(void)T;
	(void)Available;
	return FUIVector2{ 0.f, 0.f };   // 正常流里零尺寸：弹层内容活在第二个窗口
}

IUITranslator::FUIPopupAnchor FUIPopup::ResolveAnchor() const
{
	// 显式锚点（含零尺寸的点锚点：右键菜单的指针位置）优先；否则跟随父节点矩形；
	// 都没有 = 没锚点（矩形空，后端不落位）—— 故"有没有锚点"必须与矩形尺寸分开报。
	if (bHasAnchor) { return IUITranslator::FUIPopupAnchor{ true, Anchor }; }
	const FUIBuilder* Host = GetParent();
	if (bFollowAnchor && Host != nullptr)
	{
		const FUIRect Rect = Host->GetRect();
		if (!Rect.IsEmpty()) { return IUITranslator::FUIPopupAnchor{ true, Rect }; }
	}
	return IUITranslator::FUIPopupAnchor{};
}

void FUIPopup::PaintContent(IUITranslator& T, const FUIResolvedStyle& S)
{
	const IUITranslator::FUIPopupAnchor Anchor = ResolveAnchor();

	// 内容尺寸**先量**：弹层窗口的落位（贴锚点上沿 / 放不下翻到锚点下沿）要用它。量在 `BeginPopup`
	// 之前是必须的 —— 翻译器一帧一实例，记不住上一帧的高度，后端无从事后取回这个尺寸。
	// 量到的只是**子树**尺寸：本节点声明的内边距不在其中，由后端压成弹层窗口的内边距加到内容之外
	// （`FImGuiTranslator::PushWindowStyle`），故这里报出去的矩形就是纯内容矩形 —— 后端算弹层高度
	// 要的是"内容高"，多补一段内边距就会算高，弹层下沿压住锚点（而锚点常正是被补全的那个输入框）。
	// 所在坐标空间与 `FUITooltip` 不同：内容起点即弹层窗口的**内容原点**（后端压的窗口内边距
	// 就是本节点声明的 `Padding`），故子树从 (0,0) 摆起。这里再自己偏移一次声明的内边距，屏上就是
	// "后端内边距 + 声明内边距"两倍内缩 —— 弹层左上那片空档正是它。
	const float Width = (MeasureWidth > 0.f) ? MeasureWidth : kPopupMeasureWidth;
	const FUIVector2 Content = FUIBuilder::MeasureContent(T, FUIVector2{ Width, 0.f });
	const FUIRect Body{ 0.f, 0.f, Content.X, Content.Y };

	const bool bShown = T.BeginPopup(GetId(), bOpen, bShownLastFrame, Anchor, bModal, Body, S);
	// 后端刚报的事实就是下一帧的"上一帧"：**必须**在早退之前落账，否则 `bShownLastFrame` 一直是
	// false，`BeginPopup` 每帧都当成上升沿重新开窗 —— 弹层永远关不掉（旧病）。
	bShownLastFrame = bShown;
	if (!bShown)
	{
		// 后端没开/发现已关：用户点外部或 Esc 关掉了 -> 落回节点状态并入队
		if (bOpen)
		{
			bOpen = false;
			Detail::Enqueue(*this, T, EUIEventType::PopupClosed);
		}
		return;
	}

	FUILayoutEngine::ArrangeIn(*this, T, Body);
	T.EndPopup();
}

void FUIPopup::SyncConfig(const FUIBuilder& Declared)
{
	const FUIPopup* Other = dynamic_cast<const FUIPopup*>(&Declared);
	if (Other == nullptr) { return; }
	Anchor = Other->Anchor;
	bModal = Other->bModal;
	bHasAnchor = Other->bHasAnchor;
	bFollowAnchor = Other->bFollowAnchor;
	MeasureWidth = Other->MeasureWidth;
	// 开合由声明侧（业务状态）决定：节点自己因用户关闭而置 false 后，业务不置回就一直关着
	bOpen = Other->bOpen;
}

}} // namespace Maho::UI
