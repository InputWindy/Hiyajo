#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 按钮：底/边/圆角 + 图标/标签。点击事件走多播 `Clicked`。 */
class MAHO_UI_API FUIButton final : public FUIBuilder
{
public:
	explicit FUIButton(FUIName InId);
	~FUIButton() override;

	FUIButton& SetLabel(std::string_view InLabel) { Label.assign(InLabel); return *this; }
	FUIButton& SetIcon(FUIName InIcon) { Icon = InIcon; return *this; }
	FUIButton& SetIconOnly(bool bIn) { bIconOnly = bIn; return *this; }
	FUIButton& SetRepeat(bool bIn) { bRepeat = bIn; return *this; }
	FUIButton& OnClick(FUIEventHandler H) { BindClick(std::move(H)); return *this; }

	[[nodiscard]] std::string_view GetLabel() const { return Label; }
	[[nodiscard]] bool IsClickedThisFrame() const { return GetState().bPressed; }

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
	bool        bIconOnly = false;
	bool        bRepeat = false;
};

}} // namespace Maho::UI
