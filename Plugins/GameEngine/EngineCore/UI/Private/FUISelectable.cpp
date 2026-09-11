// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUISelectable.h>

#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

FUISelectable::FUISelectable(FUIName InId)
	: FUIBuilder(InId)
{
}

FUISelectable::~FUISelectable() = default;

std::string_view FUISelectable::TypeName() const
{
	return "FUISelectable";
}

const FUIStyle& FUISelectable::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		S[EUIState::Normal].Text      = Th.Text;
		S[EUIState::Normal].FontSize  = Th.FontSize;
		S[EUIState::Normal].Radius    = 2.f;
		S[EUIState::Normal].Padding   = FMargin(6.f, 3.f);
		S[EUIState::Normal].IconSize  = 14.f;

		// 悬停/按下取主色（`Accent`）而不是控件灰：本类型是"列表项/菜单项"，ImGui 自身的 selectable
		// 悬停（`ImGuiCol_HeaderHovered`）用的就是这块主色蓝。原先给 `ControlHover` 的后果是弹层里的
		// 菜单项悬停只浮一层灰，和同一弹层里被声明选中的行（`Selected` = 主色）看着不是一套东西。
		// 按下与悬停同色：两者都由指针落在本行触发（`Selected` 才是键盘/业务高亮），不必再分明暗。
		S[EUIState::Hovered].Fill = Th.Accent;
		S[EUIState::Hovered].Text = Th.Text;
		S[EUIState::Hovered].Icon = Th.IconAsset;

		S[EUIState::Pressed].Fill = Th.Accent;
		S[EUIState::Pressed].Text = Th.Text;

		S[EUIState::Selected].Fill = Th.Accent;
		S[EUIState::Selected].Text = Th.Text;

		S[EUIState::Disabled].Text = Th.TextDisabled;
	});
}

FUIVector2 FUISelectable::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	const FUIResolvedStyle& S = GetResolvedStyle();
	FUIVector2 Out{ 0.f, S.FontSize * 1.5f };

	if (!Label.empty())
	{
		const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
		const FUIVector2 TextSize = T.MeasureText(Label, Font, S.FontSize);
		Out.X += TextSize.X;
		Out.Y = std::max(Out.Y, TextSize.Y);
	}
	if (!Icon.IsNone())
	{
		Out.X += S.IconSize + (Out.X > 0.f ? 6.f : 0.f);
		Out.Y = std::max(Out.Y, S.IconSize);
	}
	if (bSpanAll) { Out.X = Available.X; }   // 撑满一行（列表项常见需求）
	return Out;
}

void FUISelectable::PaintSelf(IUITranslator& T, const FUIResolvedStyle& S)
{
	const FUIHitResult Hit = T.WidgetSelectable(GetId(), GetRect(), IsSelected(), S);
	Detail::WriteHit(*this, Hit);
	if (Hit.bClicked) { Detail::Enqueue(*this, T, EUIEventType::Clicked); }

	const FUIName IconRes = Icon.IsNone() ? S.Icon : Icon;
	if (!IconRes.IsNone())
	{
		const FUIResolvedResource Res = T.ResolveTexture(IconRes);
		if (Res.bValid) { T.DrawIcon(Detail::IconRect(*this), Res, S); }
	}
}

void FUISelectable::PaintContent(IUITranslator& T, const FUIResolvedStyle& S)
{
	if (Label.empty()) { return; }
	const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
	const FUIRect Text = Detail::LabelRect(*this, !Icon.IsNone() || !S.Icon.IsNone());
	T.DrawText(Text, Label, Font, S.FontSize, S, EUITextAlign::Left);
}

EUIInputFlags FUISelectable::GetInputFlags() const
{
	return FUIBuilder::GetInputFlags() | EUIInputFlags::HitTest;
}

void FUISelectable::SyncConfig(const FUIBuilder& Declared)
{
	const FUISelectable* Other = dynamic_cast<const FUISelectable*>(&Declared);
	if (Other == nullptr) { return; }
	Label = Other->Label;
	Icon = Other->Icon;
	SetSelected(Other->IsSelected());
	bSpanAll = Other->bSpanAll;
}

}} // namespace Maho::UI
