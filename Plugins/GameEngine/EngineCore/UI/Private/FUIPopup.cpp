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
		S[EUIState::Normal].Fill      = Th.PanelFill;
		S[EUIState::Normal].Stroke    = Th.PanelStroke;
		S[EUIState::Normal].Text      = Th.Text;
		S[EUIState::Normal].FontSize  = Th.FontSize;
		S[EUIState::Normal].Radius    = Th.Radius;
		S[EUIState::Normal].StrokeWidth = Th.StrokeWidth;
		S[EUIState::Normal].Padding   = FMargin(10.f, 8.f);

		S[EUIState::Hovered].Fill = Th.PanelFill;
		S[EUIState::Hovered].Text = Th.Text;
		S[EUIState::Pressed].Fill = Th.PanelFill;
		S[EUIState::Pressed].Text = Th.Text;
		S[EUIState::Selected].Fill = Th.PanelFill;
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

FUIRect FUIPopup::ResolveAnchorRect() const
{
	if (bHasAnchor) { return Anchor; }
	const FUIBuilder* Host = GetParent();
	if (bFollowAnchor && Host != nullptr) { return Host->GetRect(); }
	return GetRect();
}

void FUIPopup::PaintContent(IUITranslator& T, const FUIResolvedStyle& S)
{
	(void)S;
	const FUIRect Anchor = ResolveAnchorRect();
	if (!T.BeginPopup(GetId(), bOpen, Anchor, bModal))
	{
		// 后端没开/发现已关：用户点外部或 Esc 关掉了 -> 落回节点状态并入队
		if (bOpen)
		{
			bOpen = false;
			Detail::Enqueue(*this, T, EUIEventType::PopupClosed);
		}
		return;
	}

	const FUIVector2 Raw = FUIBuilder::MeasureContent(T, FUIVector2{ kPopupMeasureWidth, 0.f });
	const FUIRect Body = FUILayoutEngine::ContentRect(*this, FUIRect{ 0.f, 0.f, Raw.X, Raw.Y });
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
	// 开合由声明侧（业务状态）决定：节点自己因用户关闭而置 false 后，业务不置回就一直关着
	bOpen = Other->bOpen;
}

}} // namespace Maho::UI
