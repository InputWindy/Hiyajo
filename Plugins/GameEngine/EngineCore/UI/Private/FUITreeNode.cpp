// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUITreeNode.h>

#include "UILayoutEngine.h"
#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

FUITreeNode::FUITreeNode(FUIName InId)
	: FUIBuilder(InId)
{
}

FUITreeNode::~FUITreeNode() = default;

std::string_view FUITreeNode::TypeName() const
{
	return "FUITreeNode";
}

const FUIStyle& FUITreeNode::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		S[EUIState::Normal].Text     = Th.Text;
		S[EUIState::Normal].FontSize = Th.FontSize;
		S[EUIState::Normal].Padding  = FMargin(4.f, 2.f);
		S[EUIState::Normal].Radius   = Th.Radius;
		S[EUIState::Normal].Icon     = Th.IconChevron;
		S[EUIState::Normal].IconSize = 14.f;

		S[EUIState::Hovered].Fill = Th.ControlHover;
		S[EUIState::Hovered].Text = Th.Text;
		S[EUIState::Hovered].Icon = Th.IconChevron;

		S[EUIState::Pressed].Fill = Th.ControlPress;
		S[EUIState::Pressed].Text = Th.Text;
		S[EUIState::Pressed].Icon = Th.IconChevron;

		S[EUIState::Selected].Fill = Th.Accent;
		S[EUIState::Selected].Text = Th.Text;
		S[EUIState::Selected].Icon = Th.IconChevron;

		S[EUIState::Disabled].Text = Th.TextDisabled;
		S[EUIState::Disabled].Icon = Th.IconChevron;
	});
}

float FUITreeNode::RowHeight() const
{
	const FUIResolvedStyle& S = GetResolvedStyle();
	return std::max(S.IconSize + 4.f, S.FontSize * 1.8f);
}

FUIVector2 FUITreeNode::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	const float Row = RowHeight();
	if (!bOpen) { return FUIVector2{ 0.f, Row }; }

	const FUIVector2 Kids = FUIBuilder::MeasureContent(T, Available);
	return FUIVector2{ Kids.X + Indent, Row + Kids.Y };
}

void FUITreeNode::PaintSelf(IUITranslator& T, const FUIResolvedStyle& S)
{
	// 整行可点：命中 -> 回写状态 -> 切换展开并入队 Toggled
	const FUIHitResult Hit = T.WidgetButton(GetId(), GetRect(), S);
	Detail::WriteHit(*this, Hit);
	if (Hit.bClicked)
	{
		bOpen = !bOpen;
		Detail::Enqueue(*this, T, EUIEventType::TreeNodeToggled, 0.f, bOpen);
	}

	const FUIRect Content = Detail::ContentRectOf(*this);
	const float Row = RowHeight();
	const float IconY = Content.Y + (Row - S.IconSize) * 0.5f;

	// 箭头：展开用强调色、收起用前景色（v1 不做旋转，同图标两色区分）
	FUIResolvedStyle Arrow = S;
	Arrow.Text = bOpen ? GetUITheme().Accent : S.Text;
	const FUIRect ArrowRect{ Content.X, IconY, S.IconSize, S.IconSize };
	if (const FUIResolvedResource Res = T.ResolveTexture(S.Icon); Res.bValid)
	{
		T.DrawIcon(ArrowRect, Res, Arrow);
	}

	// 行图标（可选；缺省取主题目录图标）
	float LabelX = S.IconSize + 4.f;
	const FUIName RowIcon = Icon.IsNone() ? GetUITheme().IconFolder : Icon;
	if (const FUIResolvedResource Res = T.ResolveTexture(RowIcon); Res.bValid)
	{
		T.DrawIcon(FUIRect{ Content.X + LabelX, IconY, S.IconSize, S.IconSize }, Res, S);
		LabelX += S.IconSize + 6.f;
	}

	const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
	const FUIRect LabelRect{ Content.X + LabelX, Content.Y, std::max(0.f, Content.W - LabelX), Row };
	T.DrawText(LabelRect, Label, Font, S.FontSize, S, EUITextAlign::Left);
}

void FUITreeNode::ArrangeChildren(IUITranslator& T)
{
	if (!bOpen) { return; }   // 收起：子树既不排布也不绘制

	const FUIRect Content = Detail::ContentRectOf(*this);
	const float Row = RowHeight();
	const FUIRect Body{ Content.X + Indent, Content.Y + Row,
						std::max(0.f, Content.W - Indent), std::max(0.f, Content.H - Row) };
	FUILayoutEngine::ArrangeIn(*this, T, Body);
}

void FUITreeNode::SyncConfig(const FUIBuilder& Declared)
{
	const FUITreeNode* Other = dynamic_cast<const FUITreeNode*>(&Declared);
	if (Other == nullptr) { return; }
	Label = Other->Label;
	Icon = Other->Icon;
	bOpen = Other->bOpen;
	Indent = Other->Indent;
}

}} // namespace Maho::UI
