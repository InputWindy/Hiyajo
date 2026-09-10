#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 复选框：勾选框 + 标签。勾选态是结构化字段。 */
class MAHO_UI_API FUICheckbox final : public FUIBuilder
{
public:
	explicit FUICheckbox(FUIName InId);
	~FUICheckbox() override;

	FUICheckbox& SetLabel(std::string_view InLabel) { Label.assign(InLabel); return *this; }
	FUICheckbox& SetChecked(bool bIn) { bChecked = bIn; return *this; }
	FUICheckbox& SetIcon(FUIName InIcon) { Icon = InIcon; return *this; }
	FUICheckbox& OnToggled(FUIBoolEventHandler H) { BindToggled(std::move(H)); return *this; }

	[[nodiscard]] bool IsChecked() const { return bChecked; }
	[[nodiscard]] std::string_view GetLabel() const { return Label; }

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
	FUIName     Icon{};
	bool        bChecked = false;
	float       BoxSize = 16.f;
};

}} // namespace Maho::UI
