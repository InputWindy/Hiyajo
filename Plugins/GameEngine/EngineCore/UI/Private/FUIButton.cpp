// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUIButton.h>

#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

FUIButton::FUIButton(FUIName InId)
	: FUIBuilder(InId)
{
}

FUIButton::~FUIButton() = default;

std::string_view FUIButton::TypeName() const
{
	return "FUIButton";
}

const FUIStyle& FUIButton::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		S[EUIState::Normal].Fill     = Th.ControlFill;
		S[EUIState::Normal].Stroke   = Th.Border;
		S[EUIState::Normal].Text     = Th.Text;
		S[EUIState::Normal].FontSize = Th.FontSize;
		S[EUIState::Normal].Radius   = Th.Radius;
		S[EUIState::Normal].StrokeWidth = Th.StrokeWidth;
		S[EUIState::Normal].Padding  = FMargin(10.f, 5.f);
		S[EUIState::Normal].IconSize = 14.f;

		S[EUIState::Hovered].Fill   = Th.ControlHover;
		S[EUIState::Hovered].Stroke = Th.Border;
		S[EUIState::Hovered].Text   = Th.Text;

		S[EUIState::Pressed].Fill   = Th.ControlPress;
		S[EUIState::Pressed].Stroke = Th.Accent;
		S[EUIState::Pressed].Text   = Th.Text;

		S[EUIState::Selected].Fill   = Th.Accent;
		S[EUIState::Selected].Stroke = Th.Accent;
		S[EUIState::Selected].Text   = Th.Text;

		S[EUIState::Disabled].Fill = Th.ControlFill;
		S[EUIState::Disabled].Text = Th.TextDisabled;
	});
}

FUIVector2 FUIButton::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	(void)Available;
	const FUIResolvedStyle& S = GetResolvedStyle();
	FUIVector2 Out{ 0.f, 0.f };

	if (!bIconOnly && !Label.empty())
	{
		const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
		Out = T.MeasureText(Label, Font, S.FontSize);
	}
	if (!Icon.IsNone())
	{
		Out.X += S.IconSize + (Out.X > 0.f ? 6.f : 0.f);
		Out.Y = std::max(Out.Y, S.IconSize);
	}
	return Out;
}

void FUIButton::PaintSelf(IUITranslator& T, const FUIResolvedStyle& S)
{
	// 命中 + 状态回写 + 点击入队（底/边/圆角由后端按解析样式画）
	const FUIHitResult Hit = Detail::HitAndReport(*this, T, true);
	(void)Hit;

	if (!Icon.IsNone())
	{
		const FUIResolvedResource Res = T.ResolveTexture(S.Icon.IsNone() ? Icon : S.Icon);
		if (Res.bValid) { T.DrawIcon(Detail::IconRect(*this), Res, S); }
	}
}

void FUIButton::PaintContent(IUITranslator& T, const FUIResolvedStyle& S)
{
	if (bIconOnly || Label.empty()) { return; }

	const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
	const FUIRect Text = Detail::LabelRect(*this, !Icon.IsNone());
	T.DrawText(Text, Label, Font, S.FontSize, S, EUITextAlign::Center);
}

EUIInputFlags FUIButton::GetInputFlags() const
{
	return FUIBuilder::GetInputFlags() | EUIInputFlags::HitTest;
}

void FUIButton::SyncConfig(const FUIBuilder& Declared)
{
	const FUIButton* Other = dynamic_cast<const FUIButton*>(&Declared);
	if (Other == nullptr) { return; }
	Label = Other->Label;
	Icon = Other->Icon;
	bIconOnly = Other->bIconOnly;
	bRepeat = Other->bRepeat;
}

}} // namespace Maho::UI
