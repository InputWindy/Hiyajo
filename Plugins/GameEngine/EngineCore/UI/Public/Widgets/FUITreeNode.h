#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 树节点：一行标题（箭头 + 图标 + 标签）+ 展开时的缩进子树。
 *  子节点只在展开时参与排布；展开态是结构化字段（跨帧持久）。 */
class MAHO_UI_API FUITreeNode final : public FUIBuilder
{
public:
	explicit FUITreeNode(FUIName InId);
	~FUITreeNode() override;

	FUITreeNode& SetLabel(std::string_view InLabel) { Label.assign(InLabel); return *this; }
	FUITreeNode& SetOpen(bool bIn) { bOpen = bIn; return *this; }
	FUITreeNode& SetIcon(FUIName InIcon) { Icon = InIcon; return *this; }
	FUITreeNode& SetIndent(float InIndent) { Indent = InIndent; return *this; }
	FUITreeNode& OnToggled(FUIBoolEventHandler H) { BindToggled(std::move(H)); return *this; }

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
	float       Indent = 14.f;
};

}} // namespace Maho::UI
