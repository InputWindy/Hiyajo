#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 拖拽数值（1..4 分量，语义等价 ImGui::DragFloatN）：标签在左、拖拽条占右。
 *
 *  值与后端的关系：翻译期后端**原地改写本节点的 `Values`**，故所有者读回请用
 *  `GetValues()`（拖拽中的值就是运行期真值，不会被下一帧的声明压回 —— 只有
 *  `SetValue/SetValues` 显式给过才覆盖）。多播 `ValueChanged` 只是糖：
 *  1 分量时携带该值，多分量时携带第 0 分量。 */
class MAHO_UI_API FUIDragFloat final : public FUIBuilder
{
public:
	explicit FUIDragFloat(FUIName InId);
	~FUIDragFloat() override;

	FUIDragFloat& SetLabel(std::string_view InLabel) { Label.assign(InLabel); return *this; }
	/** 分量数（1..4）；越界按 1 / 4 收口。 */
	FUIDragFloat& SetComponents(int InComponents);
	/** 单分量赋值（`Components == 1` 时用）。 */
	FUIDragFloat& SetValue(float InValue);
	/** N 分量赋值（读 `Components` 个 float；nullptr 忽略）。 */
	FUIDragFloat& SetValues(const float* InValues);
	FUIDragFloat& SetSpeed(float InSpeed) { Speed = InSpeed; return *this; }
	FUIDragFloat& SetFormat(std::string_view InFormat) { Format.assign(InFormat); return *this; }
	FUIDragFloat& SetLabelWidth(float W) { LabelWidth = W; return *this; }
	FUIDragFloat& OnValueChanged(FUIFloatEventHandler H) { BindValueChanged(std::move(H)); return *this; }

	[[nodiscard]] float GetValue() const { return Values[0]; }
	[[nodiscard]] const float* GetValues() const { return Values; }
	[[nodiscard]] int GetComponents() const { return Components; }

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
	float Values[4] = { 0.f, 0.f, 0.f, 0.f };
	int   Components = 1;
	float Speed = 0.25f;
	float LabelWidth = 0.f;
	/** 声明侧是否显式给过值：块复用同步时只认显式声明，不拿陈旧声明压回后端写入的运行期值。 */
	bool  bValueDirty = false;
};

}} // namespace Maho::UI
