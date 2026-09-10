#pragma once

#include "FUIBuilder.h"

#include <optional>

namespace Maho { namespace UI {

/** 分隔线：横向为一条细线，纵向（`Layout().Height` 大 / `SetVertical`）为竖线。
 *  颜色缺省取主题 `Separator`。 */
class MAHO_UI_API FUISeparator final : public FUIBuilder
{
public:
	explicit FUISeparator(FUIName InId);
	~FUISeparator() override;

	FUISeparator& SetThickness(float InThickness) { Thickness = InThickness; return *this; }
	FUISeparator& SetColor(FUIColor InColor) { Color = InColor; return *this; }
	FUISeparator& SetVertical(bool bIn) { bVertical = bIn; return *this; }

	[[nodiscard]] bool HasColor() const { return Color.has_value(); }

protected:
	std::string_view TypeName() const override;
	FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override;
	void       PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override;
	const FUIStyle& TypeDefaultStyle() const override;
	void       SyncConfig(const FUIBuilder& Declared) override;

private:
	std::optional<FUIColor> Color;
	float Thickness = 1.f;
	bool  bVertical = false;
};

}} // namespace Maho::UI
