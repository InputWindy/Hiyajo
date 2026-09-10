#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 单行文本：内容尺寸由翻译后端测量（字体是资源引用，逐实例可换）。 */
class MAHO_UI_API FUIText final : public FUIBuilder
{
public:
	explicit FUIText(FUIName InId);
	~FUIText() override;

	FUIText& SetText(std::string_view InText) { Text.assign(InText); return *this; }
	FUIText& SetAlign(EUITextAlign A) { Align = A; return *this; }
	FUIText& SetWrap(bool bIn) { bWrap = bIn; return *this; }
	FUIText& SetFont(FUIName F) { Style().Font = F; return *this; }
	FUIText& SetFontSize(float Size) { Style()[EUIState::Normal].FontSize = Size; return *this; }

	[[nodiscard]] std::string_view GetText() const { return Text; }

protected:
	std::string_view TypeName() const override;
	FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override;
	void       PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override;
	const FUIStyle& TypeDefaultStyle() const override;
	void       SyncConfig(const FUIBuilder& Declared) override;

private:
	std::string  Text;
	EUITextAlign Align = EUITextAlign::Left;
	bool         bWrap = false;
};

}} // namespace Maho::UI
