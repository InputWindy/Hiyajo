#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 悬停提示：自身在正常流里**零尺寸**（占位不占空间）；当父节点被悬停时，
 *  由翻译器开一个提示窗口，内容 = `SetText` 的文本（或本节点的子树）。
 *  面板侧零后端代码。 */
class MAHO_UI_API FUITooltip final : public FUIBuilder
{
public:
	explicit FUITooltip(FUIName InId);
	~FUITooltip() override;

	FUITooltip& SetText(std::string_view InText) { Text.assign(InText); bHasText = true; return *this; }
	/** 提示内容改用子树（`[]` 或 `AddItem`）时调用：不再画 `SetText` 的文本。 */
	FUITooltip& SetChildren() { bHasText = false; return *this; }
	FUITooltip& SetDelay(float InDelay) { Delay = InDelay; return *this; }
	FUITooltip& SetFollowMouse(bool bIn) { bFollowMouse = bIn; return *this; }

	[[nodiscard]] std::string_view GetText() const { return Text; }

protected:
	std::string_view TypeName() const override;
	FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override;
	void       PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override;
	const FUIStyle& TypeDefaultStyle() const override;
	void       SyncConfig(const FUIBuilder& Declared) override;
	bool       IsOverlayLayer() const override { return true; }

private:
	/** 提示窗口的内容尺寸（文本或子树），与正常流的零尺寸测量分开。 */
	[[nodiscard]] FUIVector2 MeasureTipContent(IUITranslator& T) const;

	std::string Text;
	float       Delay = 0.f;      // v1：后端尽力而为（ImGui 即时提示，不排队等待）
	bool        bFollowMouse = true;
	bool        bHasText = true;
};

}} // namespace Maho::UI
