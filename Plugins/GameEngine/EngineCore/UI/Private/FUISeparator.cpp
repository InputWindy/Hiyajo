// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUISeparator.h>

#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

FUISeparator::FUISeparator(FUIName InId)
	: FUIBuilder(InId)
{
}

FUISeparator::~FUISeparator() = default;

std::string_view FUISeparator::TypeName() const
{
	return "FUISeparator";
}

const FUIStyle& FUISeparator::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		S[EUIState::Normal].Text      = Th.Text;
		S[EUIState::Normal].FontSize  = Th.FontSize;
		S[EUIState::Hovered].Text     = Th.Text;
		S[EUIState::Pressed].Text     = Th.Text;
		S[EUIState::Selected].Text    = Th.Text;
		S[EUIState::Disabled].Text    = Th.TextDisabled;
	});
}

FUIVector2 FUISeparator::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	(void)T;
	if (bVertical) { return FUIVector2{ Thickness, std::max(Available.Y, Thickness) }; }
	return FUIVector2{ std::max(Available.X, Thickness), Thickness };
}

void FUISeparator::PaintSelf(IUITranslator& T, const FUIResolvedStyle& S)
{
	const FUIRect R = GetRect();
	FUIResolvedStyle LineStyle{};
	LineStyle.Fill = Color.has_value() ? *Color : GetUITheme().Separator;
	if (!Color.has_value()) { LineStyle.Fill.A *= S.Text.A; }
	LineStyle.Radius = 0.f;

	if (bVertical)
	{
		T.DrawRect(FUIRect{ R.X + (R.W - Thickness) * 0.5f, R.Y, Thickness, R.H }, LineStyle);
	}
	else
	{
		T.DrawRect(FUIRect{ R.X, R.Y + (R.H - Thickness) * 0.5f, R.W, Thickness }, LineStyle);
	}
}

void FUISeparator::SyncConfig(const FUIBuilder& Declared)
{
	const FUISeparator* Other = dynamic_cast<const FUISeparator*>(&Declared);
	if (Other == nullptr) { return; }
	Color = Other->Color;
	Thickness = Other->Thickness;
	bVertical = Other->bVertical;
}

}} // namespace Maho::UI
