// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUITooltip.h>

#include "UILayoutEngine.h"
#include "WidgetStyle.h"

namespace Maho { namespace UI {

namespace
{
/** 提示内容测量的宽度上限（弹窗自体自适应尺寸，只需一个不撑爆的界）。 */
constexpr float kTipMeasureWidth = 480.f;
} // namespace

FUITooltip::FUITooltip(FUIName InId)
	: FUIBuilder(InId)
{
}

FUITooltip::~FUITooltip() = default;

std::string_view FUITooltip::TypeName() const
{
	return "FUITooltip";
}

const FUIStyle& FUITooltip::TypeDefaultStyle() const
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
		S[EUIState::Normal].Padding   = FMargin(8.f, 5.f);

		S[EUIState::Hovered].Fill = Th.PanelFill;
		S[EUIState::Hovered].Text = Th.Text;
		S[EUIState::Pressed].Fill = Th.PanelFill;
		S[EUIState::Pressed].Text = Th.Text;
		S[EUIState::Selected].Fill = Th.PanelFill;
		S[EUIState::Selected].Text = Th.Text;
		S[EUIState::Disabled].Text = Th.TextDisabled;
	});
}

FUIVector2 FUITooltip::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	(void)T;
	(void)Available;
	return FUIVector2{ 0.f, 0.f };   // 正常流里零尺寸：占位不占空间
}

FUIVector2 FUITooltip::MeasureTipContent(IUITranslator& T) const
{
	const FUIResolvedStyle& S = GetResolvedStyle();
	FUIVector2 Out{ 0.f, 0.f };

	if (bHasText && !Text.empty())
	{
		const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
		Out = T.MeasureText(Text, Font, S.FontSize);
	}
	else
	{
		// 子树内容：用同一套布局量原始内容尺寸（本节点 Padding 在下面一次性加回）
		Out = FUIBuilder::MeasureContent(T, FUIVector2{ kTipMeasureWidth, 0.f });
	}

	const FMargin& Pad = S.Padding;
	return FUIVector2{ Out.X + Pad.Left + Pad.Right, Out.Y + Pad.Top + Pad.Bottom };
}

void FUITooltip::PaintContent(IUITranslator& T, const FUIResolvedStyle& S)
{
	// 悬浮才开提示窗：宿主使用约定 = 提示节点是"被悬停控件"的子节点，
	// 该控件的命中状态在本帧早于子节点翻译（PaintSelf 先于 ArrangeChildren）。
	const FUIBuilder* Host = GetParent();
	if (Host == nullptr || !Host->GetState().bHovered) { return; }
	if (!bHasText && GetChildren().empty()) { return; }

	// v1：`Delay` 由后端即时提示（ImGui 不排队计时），字段先落在节点上
	const FUIRect Anchor = Host->GetRect();
	if (!T.BeginTooltip(GetId(), Anchor, bFollowMouse)) { return; }

	const FUIVector2 Size = MeasureTipContent(T);
	const FUIRect Body{ 0.f, 0.f, Size.X, Size.Y };
	if (bHasText && !Text.empty())
	{
		const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
		T.DrawText(Body, Text, Font, S.FontSize, S, EUITextAlign::Left);
	}
	else
	{
		FUILayoutEngine::ArrangeIn(*this, T, FUILayoutEngine::ContentRect(*this, Body));
	}
	T.EndTooltip();
}

void FUITooltip::SyncConfig(const FUIBuilder& Declared)
{
	const FUITooltip* Other = dynamic_cast<const FUITooltip*>(&Declared);
	if (Other == nullptr) { return; }
	Text = Other->Text;
	Delay = Other->Delay;
	bFollowMouse = Other->bFollowMouse;
	bHasText = Other->bHasText;
}

}} // namespace Maho::UI
