#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 颜色编辑（RGBA 四分量，语义等价 ImGui::ColorEdit4 + AlphaBar）：标签在左、色块占右。
 *
 *  值与后端的关系：翻译期后端**原地改写本节点的 `RGBA`**，故所有者读回请用
 *  `GetValue()`（编辑中的颜色就是运行期真值，不会被下一帧的声明压回 —— 只有
 *  `SetValue` 显式给过才覆盖）。颜色编辑没有单值语义，故不抛 `ValueChanged`。 */
class MAHO_UI_API FUIColorEdit final : public FUIBuilder
{
public:
	explicit FUIColorEdit(FUIName InId);
	~FUIColorEdit() override;

	FUIColorEdit& SetLabel(std::string_view InLabel) { Label.assign(InLabel); return *this; }
	FUIColorEdit& SetValue(const FUIColor& InColor);
	FUIColorEdit& SetLabelWidth(float W) { LabelWidth = W; return *this; }

	[[nodiscard]] FUIColor GetValue() const;

protected:
	std::string_view TypeName() const override;
	FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override;
	void       PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override;
	void       PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override;
	EUIInputFlags GetInputFlags() const override;
	const FUIStyle& TypeDefaultStyle() const override;
	void       SyncConfig(const FUIBuilder& Declared) override;

private:
	std::string Label;
	float RGBA[4] = { 1.f, 1.f, 1.f, 1.f };
	float LabelWidth = 0.f;
	/** 声明侧是否显式给过值：块复用同步时只认显式声明，不拿陈旧声明压回后端写入的运行期值。 */
	bool  bValueDirty = false;
};

}} // namespace Maho::UI
