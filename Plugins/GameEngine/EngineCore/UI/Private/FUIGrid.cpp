// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUIGrid.h>

#include "WidgetStyle.h"

#include <algorithm>
#include <cmath>

namespace Maho { namespace UI {

FUIGrid::FUIGrid(FUIName InId)
	: FUIBuilder(InId)
{
}

FUIGrid::~FUIGrid() = default;

std::string_view FUIGrid::TypeName() const
{
	return "FUIGrid";
}

namespace
{
/** 单元尺寸：显式 `SetCellSize` 优先，否则由可用宽度均分（高缺省与宽同值 = 方形）。 */
FUIVector2 CellSizeOf(const FUIGrid& Grid, const FUIVector2& Available)
{
	const int Cols = std::max(1, Grid.GetColumns());
	const float Spacing = Grid.GetCellSpacing();
	float W = Grid.GetCellSize().X;
	float H = Grid.GetCellSize().Y;
	if (W <= 0.f) { W = std::max(1.f, (Available.X - Spacing * static_cast<float>(Cols - 1)) / static_cast<float>(Cols)); }
	if (H <= 0.f) { H = W; }
	return FUIVector2{ W, H };
}

int VisibleCountOf(const FUIGrid& Grid)
{
	int Count = 0;
	for (const auto& Child : Grid.GetChildren())
	{
		if (Child->IsVisible()) { ++Count; }
	}
	return Count;
}
} // namespace

FUIVector2 FUIGrid::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	(void)T;
	const int Cols = std::max(1, GetColumns());
	const int Count = VisibleCountOf(*this);
	if (Count == 0) { return FUIVector2{ 0.f, 0.f }; }

	const FUIVector2 Cell = CellSizeOf(*this, Available);
	const float Spacing = GetCellSpacing();
	const int Rows = (Count + Cols - 1) / Cols;
	return FUIVector2{ Cell.X * static_cast<float>(Cols) + Spacing * static_cast<float>(Cols - 1),
					   Cell.Y * static_cast<float>(Rows) + Spacing * static_cast<float>(Rows - 1) };
}

void FUIGrid::ArrangeChildren(IUITranslator& T)
{
	const int Cols = std::max(1, GetColumns());
	const FUIRect Content = Detail::ContentRectOf(*this);
	const FUIVector2 Cell = CellSizeOf(*this, FUIVector2{ Content.W, Content.H });
	const float Spacing = GetCellSpacing();

	int Index = 0;
	for (const auto& ChildPtr : GetChildren())
	{
		FUIBuilder& Child = *ChildPtr;
		if (!Child.IsVisible()) { continue; }

		const int Col = Index % Cols;
		const int Row = Index / Cols;
		++Index;

		const FUIRect CellRect{ Content.X + static_cast<float>(Col) * (Cell.X + Spacing),
								Content.Y + static_cast<float>(Row) * (Cell.Y + Spacing),
								Cell.X, Cell.Y };
		Child.Translate(T, CellRect);
	}
}

EUIInputFlags FUIGrid::GetInputFlags() const
{
	return FUIBuilder::GetInputFlags() | EUIInputFlags::Clip;
}

const FUIStyle& FUIGrid::TypeDefaultStyle() const
{
	// 网格本身透明：只有选中框（Selected 的 Stroke）需要取值。
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		S[EUIState::Normal].Text     = Th.Text;
		S[EUIState::Normal].FontSize = Th.FontSize;
		S[EUIState::Normal].Radius   = Th.Radius;

		S[EUIState::Hovered].Text  = Th.Text;
		S[EUIState::Pressed].Text  = Th.Text;
		S[EUIState::Selected].Text   = Th.Text;
		S[EUIState::Selected].Stroke = Th.Accent;
		S[EUIState::Disabled].Text = Th.TextDisabled;
	});
}

void FUIGrid::PaintOverlay(IUITranslator& T, const FUIResolvedStyle& S)
{
	if (SelectedId.IsNone()) { return; }

	// 选中框画在子节点之上（不写子节点状态：翻译期只读共享锁，不产生跨线程写）
	if (const FUIBuilder* Selected = FindChild(SelectedId))
	{
		const FUIRect R = Selected->GetRect();
		if (R.IsEmpty()) { return; }

		FUIResolvedStyle Frame = S;
		Frame.Fill        = FUIColor{ 0.f, 0.f, 0.f, 0.f };
		Frame.Stroke      = GetUITheme().Accent;
		Frame.StrokeWidth = 2.f;
		T.DrawRect(Detail::Inflate(R, 1.f, 1.f), Frame);
	}
}

}} // namespace Maho::UI
