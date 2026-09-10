#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 可折叠分组头：一行标题 + 展开时的子树。 */
class MAHO_UI_API FUICollapsingHeader final : public FUIBuilder
{
public:
	explicit FUICollapsingHeader(FUIName InId);
	~FUICollapsingHeader() override;

	FUICollapsingHeader& SetLabel(std::string_view InLabel) { Label.assign(InLabel); return *this; }
	FUICollapsingHeader& SetDefaultOpen(bool bIn) { bOpen = bIn; return *this; }
	FUICollapsingHeader& SetCollapsible(bool bIn) { bCollapsible = bIn; return *this; }
	FUICollapsingHeader& SetIcon(FUIName InIcon) { Icon = InIcon; return *this; }
	FUICollapsingHeader& OnToggled(FUIBoolEventHandler H) { BindToggled(std::move(H)); return *this; }

	[[nodiscard]] bool IsOpen() const { return bOpen; }
	[[nodiscard]] std::string_view GetLabel() const { return Label; }

protected:
	std::string_view TypeName() const override;
	FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override;
	void       PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override;
	void       ArrangeChildren(IUITranslator& T) override;
	const FUIStyle& TypeDefaultStyle() const override;
	void       SyncConfig(const FUIBuilder& Declared) override;

private:
	[[nodiscard]] float RowHeight() const;

	std::string Label;
	FUIName     Icon{};
	bool        bOpen = false;
	bool        bCollapsible = true;
};

}} // namespace Maho::UI
