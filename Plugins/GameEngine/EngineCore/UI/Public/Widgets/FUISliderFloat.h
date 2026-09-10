#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 浮点滑块：标签在左、滑条占右。值变更走多播 `ValueChanged`。 */
class MAHO_UI_API FUISliderFloat final : public FUIBuilder
{
public:
	explicit FUISliderFloat(FUIName InId);
	~FUISliderFloat() override;

	FUISliderFloat& SetLabel(std::string_view InLabel) { Label.assign(InLabel); return *this; }
	FUISliderFloat& SetValue(float InValue) { Value = InValue; bValueDirty = true; return *this; }
	FUISliderFloat& SetRange(float InMin, float InMax) { Min = InMin; Max = InMax; return *this; }
	FUISliderFloat& SetFormat(std::string_view InFormat) { Format.assign(InFormat); return *this; }
	FUISliderFloat& SetLabelWidth(float W) { LabelWidth = W; return *this; }
	FUISliderFloat& OnValueChanged(FUIFloatEventHandler H) { BindValueChanged(std::move(H)); return *this; }

	[[nodiscard]] float GetValue() const { return Value; }

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
	std::string Format = "%.3f";
	float Value = 0.f;
	float Min = 0.f, Max = 1.f;
	float LabelWidth = 0.f;
	/** 声明侧是否显式给过值：块复用同步时只认显式声明，不拿陈旧声明压回后端写入的运行期值。 */
	bool  bValueDirty = false;
};

}} // namespace Maho::UI
