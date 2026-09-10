#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 文本输入：单行 / 多行，带提示串与长度上限。文本是节点上的持久值。 */
class MAHO_UI_API FUIInputText final : public FUIBuilder
{
public:
	explicit FUIInputText(FUIName InId);
	~FUIInputText() override;

	FUIInputText& SetValue(std::string_view InValue) { Value.assign(InValue); bValueDirty = true; return *this; }
	FUIInputText& SetHint(std::string_view InHint) { Hint.assign(InHint); return *this; }
	FUIInputText& SetMaxLength(std::size_t InMax) { MaxLength = InMax; return *this; }
	FUIInputText& SetMultiline(bool bIn) { bMultiline = bIn; return *this; }
	FUIInputText& OnTextChanged(FUITextEventHandler H) { BindTextChanged(std::move(H)); return *this; }
	/** 单行输入框回车提交（多行不提交，交给按钮）。载荷 = 提交时的文本。 */
	FUIInputText& OnSubmitted(FUITextEventHandler H) { BindSubmitted(std::move(H)); return *this; }

	[[nodiscard]] const std::string& GetValue() const { return Value; }
	[[nodiscard]] bool HasFocus() const { return GetState().bFocused; }

protected:
	std::string_view TypeName() const override;
	FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override;
	void       PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override;
	EUIInputFlags GetInputFlags() const override;
	bool       WantsKeyboardFocus() const override { return true; }
	const FUIStyle& TypeDefaultStyle() const override;
	void       SyncConfig(const FUIBuilder& Declared) override;

private:
	std::string Value;
	std::string Hint;
	std::size_t MaxLength = 0;
	bool        bMultiline = false;
	/** 声明侧是否显式给过文本：块复用同步时只认显式声明，不拿陈旧声明压回用户输入。 */
	bool        bValueDirty = false;
};

}} // namespace Maho::UI
