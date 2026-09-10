// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUICheckbox.h>

#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

namespace
{
/** 勾选框本体：内容区左端、垂直居中。 */
FUIRect BoxRectOf(const FUIRect& Content, float BoxSize)
{
	return FUIRect{ Content.X, Content.Y + (Content.H - BoxSize) * 0.5f, BoxSize, BoxSize };
}
} // namespace

FUICheckbox::FUICheckbox(FUIName InId)
	: FUIBuilder(InId)
{
}

FUICheckbox::~FUICheckbox() = default;

std::string_view FUICheckbox::TypeName() const
{
	return "FUICheckbox";
}

const FUIStyle& FUICheckbox::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		S[EUIState::Normal].Fill      = Th.ControlFill;
		S[EUIState::Normal].Stroke    = Th.Border;
		S[EUIState::Normal].Text      = Th.Text;
		S[EUIState::Normal].FontSize  = Th.FontSize;
		S[EUIState::Normal].Radius    = 2.f;
		S[EUIState::Normal].StrokeWidth = Th.StrokeWidth;
		S[EUIState::Normal].Padding   = FMargin(4.f, 2.f);
		S[EUIState::Normal].IconSize  = 12.f;

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

FUIVector2 FUICheckbox::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	(void)Available;
	const FUIResolvedStyle& S = GetResolvedStyle();
	FUIVector2 Out{ BoxSize, BoxSize };

	if (!Label.empty())
	{
		const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
		const FUIVector2 TextSize = T.MeasureText(Label, Font, S.FontSize);
		Out.X += 6.f + TextSize.X;
		Out.Y = std::max(Out.Y, TextSize.Y);
	}
	return Out;
}

void FUICheckbox::PaintSelf(IUITranslator& T, const FUIResolvedStyle& S)
{
	// 命中整个行区：点标签也切换（后端只报命中，勾选态由这里翻转）
	const FUIHitResult Hit = T.WidgetButton(GetId(), GetRect(), FUIResolvedStyle{});
	Detail::WriteHit(*this, Hit);
	if (Hit.bClicked && !IsDisabled())
	{
		bChecked = !bChecked;
		Detail::Enqueue(*this, T, EUIEventType::Toggled, 0.f, bChecked);
	}

	const FUIRect Box = BoxRectOf(Detail::ContentRectOf(*this), BoxSize);
	FUIResolvedStyle BoxStyle = S;
	BoxStyle.Fill = bChecked ? GetUITheme().Accent : S.Fill;
	BoxStyle.Stroke = bChecked ? GetUITheme().Accent : S.Stroke;
	T.DrawRect(Box, BoxStyle);

	if (!bChecked) { return; }
	const FUIResolvedResource Res = T.ResolveTexture(Icon.IsNone() ? GetUITheme().IconCheck : Icon);
	if (Res.bValid) { T.DrawIcon(Box, Res, S); }
	else { T.DrawRect(Detail::Inflate(Box, -BoxSize * 0.3f, -BoxSize * 0.3f), BoxStyle); }
}

void FUICheckbox::PaintContent(IUITranslator& T, const FUIResolvedStyle& S)
{
	if (Label.empty()) { return; }
	const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
	const FUIRect Content = Detail::ContentRectOf(*this);
	const FUIRect Text{ Content.X + BoxSize + 6.f, Content.Y,
						std::max(0.f, Content.W - BoxSize - 6.f), Content.H };
	T.DrawText(Text, Label, Font, S.FontSize, S, EUITextAlign::Left);
}

EUIInputFlags FUICheckbox::GetInputFlags() const
{
	return FUIBuilder::GetInputFlags() | EUIInputFlags::HitTest;
}

void FUICheckbox::SyncConfig(const FUIBuilder& Declared)
{
	const FUICheckbox* Other = dynamic_cast<const FUICheckbox*>(&Declared);
	if (Other == nullptr) { return; }
	Label = Other->Label;
	Icon = Other->Icon;
	bChecked = Other->bChecked;
}

}} // namespace Maho::UI
