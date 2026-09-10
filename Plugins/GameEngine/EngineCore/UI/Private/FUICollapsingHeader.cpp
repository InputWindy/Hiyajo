// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUICollapsingHeader.h>

#include "UILayoutEngine.h"
#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

FUICollapsingHeader::FUICollapsingHeader(FUIName InId)
	: FUIBuilder(InId)
{
}

FUICollapsingHeader::~FUICollapsingHeader() = default;

std::string_view FUICollapsingHeader::TypeName() const
{
	return "FUICollapsingHeader";
}

const FUIStyle& FUICollapsingHeader::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		S[EUIState::Normal].Fill     = Th.ControlFill;
		S[EUIState::Normal].Text     = Th.Text;
		S[EUIState::Normal].FontSize = Th.FontSize;
		S[EUIState::Normal].Radius   = Th.Radius;
		S[EUIState::Normal].Padding  = FMargin(2.f, 2.f);
		S[EUIState::Normal].Icon     = Th.IconChevron;
		S[EUIState::Normal].IconSize = 14.f;

		S[EUIState::Hovered].Fill = Th.ControlHover;
		S[EUIState::Hovered].Text = Th.Text;
		S[EUIState::Hovered].Icon = Th.IconChevron;

		S[EUIState::Pressed].Fill = Th.ControlPress;
		S[EUIState::Pressed].Text = Th.Text;
		S[EUIState::Pressed].Icon = Th.IconChevron;

		S[EUIState::Selected].Fill = Th.ControlHover;
		S[EUIState::Selected].Text = Th.Text;
		S[EUIState::Selected].Icon = Th.IconChevron;

		S[EUIState::Disabled].Fill = Th.ControlFill;
		S[EUIState::Disabled].Text = Th.TextDisabled;
		S[EUIState::Disabled].Icon = Th.IconChevron;
	});
}

float FUICollapsingHeader::RowHeight() const
{
	const FUIResolvedStyle& S = GetResolvedStyle();
	return std::max(S.IconSize + 6.f, S.FontSize * 1.9f);
}

FUIVector2 FUICollapsingHeader::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	const float Row = RowHeight();
	if (!bOpen) { return FUIVector2{ 0.f, Row }; }

	const FUIVector2 Kids = FUIBuilder::MeasureContent(T, Available);
	return FUIVector2{ Kids.X, Row + Kids.Y };
}

void FUICollapsingHeader::PaintSelf(IUITranslator& T, const FUIResolvedStyle& S)
{
	const FUIRect Content = Detail::ContentRectOf(*this);
	const float Row = RowHeight();
	const FUIRect RowRect{ Content.X, Content.Y, Content.W, Row };

	// 展开交互交给后端控件（箭头/动画由它画），本节点只画标签与图标
	const bool bWasOpen = bOpen;
	const FUIHitResult Hit = T.WidgetCollapsingHeader(GetId(), RowRect, bOpen, S);
	Detail::WriteHit(*this, Hit);

	if (!bCollapsible) { bOpen = bWasOpen; }            // 不可折叠：回滚后端改动
	else if (bOpen != bWasOpen) { Detail::Enqueue(*this, T, EUIEventType::Toggled, 0.f, bOpen); }

	const float IconSize = S.IconSize;
	const float IconY = RowRect.Y + (Row - IconSize) * 0.5f;
	float LabelX = IconSize + 6.f;

	if (const FUIName RowIcon = Icon; !RowIcon.IsNone())
	{
		if (const FUIResolvedResource Res = T.ResolveTexture(RowIcon); Res.bValid)
		{
			FUIResolvedStyle IconStyle = S;
			IconStyle.IconSize = IconSize;
			T.DrawIcon(FUIRect{ RowRect.X + LabelX, IconY, IconSize, IconSize }, Res, IconStyle);
			LabelX += IconSize + 4.f;
		}
	}

	const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
	const FUIRect LabelRect{ RowRect.X + LabelX, RowRect.Y, std::max(0.f, RowRect.W - LabelX), Row };
	T.DrawText(LabelRect, Label, Font, S.FontSize, S, EUITextAlign::Left);
}

void FUICollapsingHeader::ArrangeChildren(IUITranslator& T)
{
	if (!bOpen) { return; }   // 收起：子树既不排布也不绘制

	const FUIRect Content = Detail::ContentRectOf(*this);
	const float Row = RowHeight();
	const FUIRect Body{ Content.X, Content.Y + Row, Content.W, std::max(0.f, Content.H - Row) };
	FUILayoutEngine::ArrangeIn(*this, T, Body);
}

void FUICollapsingHeader::SyncConfig(const FUIBuilder& Declared)
{
	const FUICollapsingHeader* Other = dynamic_cast<const FUICollapsingHeader*>(&Declared);
	if (Other == nullptr) { return; }
	Label = Other->Label;
	Icon = Other->Icon;
	bOpen = Other->bOpen;
	bCollapsible = Other->bCollapsible;
}

}} // namespace Maho::UI
