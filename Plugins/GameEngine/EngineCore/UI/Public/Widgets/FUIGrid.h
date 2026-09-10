#pragma once

#include "FUIBuilder.h"

namespace Maho { namespace UI {

/** 网格容器：定列数 + 定单元尺寸，按行铺子（内容浏览器资产网格）。
 *  `Toggleable` + `SelectedId` 走选中态：选中的单元用 `Selected` 状态组绘制。 */
class MAHO_UI_API FUIGrid final : public FUIBuilder
{
public:
	explicit FUIGrid(FUIName InId);
	~FUIGrid() override;

	FUIGrid& SetColumns(int InColumns) { Columns = InColumns > 0 ? InColumns : 1; return *this; }
	FUIGrid& SetCellSize(float W, float H) { CellW = W; CellH = H; return *this; }
	FUIGrid& SetToggleable(bool bIn) { bToggleable = bIn; return *this; }
	FUIGrid& SetSelected(FUIName Id) { SelectedId = Id; return *this; }
	FUIGrid& SetCellSpacing(float S) { CellSpacing = S; return *this; }

	[[nodiscard]] int     GetColumns() const { return Columns; }
	[[nodiscard]] FUIVector2 GetCellSize() const { return FUIVector2{ CellW, CellH }; }
	[[nodiscard]] float   GetCellSpacing() const { return CellSpacing; }
	[[nodiscard]] FUIName GetSelected() const { return SelectedId; }

protected:
	std::string_view TypeName() const override;
	FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override;
	void       ArrangeChildren(IUITranslator& T) override;
	void       PaintOverlay(IUITranslator& T, const FUIResolvedStyle& S) override;
	EUIInputFlags GetInputFlags() const override;
	const FUIStyle& TypeDefaultStyle() const override;

private:
	int     Columns = 1;
	float   CellW = 0.f, CellH = 0.f;
	float   CellSpacing = 4.f;
	bool    bToggleable = false;
	FUIName SelectedId{};
};

}} // namespace Maho::UI
