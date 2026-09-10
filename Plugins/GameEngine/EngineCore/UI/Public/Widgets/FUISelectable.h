#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 可选项：选中态由父容器（列表/树/网格）维护，本节点只报告点击。 */
class MAHO_UI_API FUISelectable final : public FUIBuilder
{
public:
	explicit FUISelectable(FUIName InId);
	~FUISelectable() override;

	FUISelectable& SetLabel(std::string_view InLabel) { Label.assign(InLabel); return *this; }
	FUISelectable& SetIcon(FUIName InIcon) { Icon = InIcon; return *this; }
	/** 选中态就是基类的那一份（样式解析读它）；本类型不再自存镜像，避免"传了后端、样式不换色"。 */
	FUISelectable& SetSelected(bool bIn) { FUIBuilder::SetSelected(bIn); return *this; }
	FUISelectable& SetSpanAll(bool bIn) { bSpanAll = bIn; return *this; }
	FUISelectable& OnSelected(FUIEventHandler H) { BindClick(std::move(H)); return *this; }

	[[nodiscard]] bool IsSelected() const { return FUIBuilder::IsSelected(); }
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
	bool        bSpanAll = false;
};

}} // namespace Maho::UI
