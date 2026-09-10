#pragma once

#include "FUIBuilder.h"

namespace Maho { namespace UI {

/** 弹层：自身在正常流里零尺寸；`bOpen` 由业务置位，翻译器开第二个窗口并把子树摆进去。
 *  用户点外部 / 按 Esc 关掉时，翻译器入队 `PopupClosed` —— 多播 `OnClosed` 收到后可把 `SetOpen(false)` 落回树。 */
class MAHO_UI_API FUIPopup final : public FUIBuilder
{
public:
	explicit FUIPopup(FUIName InId);
	~FUIPopup() override;

	FUIPopup& SetOpen(bool bIn) { bOpen = bIn; return *this; }
	FUIPopup& SetAnchor(const FUIRect& InAnchor) { Anchor = InAnchor; bHasAnchor = true; return *this; }
	FUIPopup& SetModal(bool bIn) { bModal = bIn; return *this; }
	FUIPopup& SetFollowAnchor(bool bIn) { bFollowAnchor = bIn; return *this; }
	FUIPopup& OnClosed(FUIEventHandler H) { BindPopupClosed(std::move(H)); return *this; }

	[[nodiscard]] bool IsOpen() const { return bOpen; }

protected:
	std::string_view TypeName() const override;
	FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override;
	void       PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override;
	const FUIStyle& TypeDefaultStyle() const override;
	void       SyncConfig(const FUIBuilder& Declared) override;
	bool       IsOverlayLayer() const override { return true; }

private:
	/** 弹层内容的期望矩形（锚点或父节点矩形；局部坐标）。 */
	[[nodiscard]] FUIRect ResolveAnchorRect() const;

	FUIRect Anchor{};
	bool    bOpen = false;
	bool    bModal = false;
	bool    bHasAnchor = false;
	bool    bFollowAnchor = true;   // 每帧跟随父节点矩形（否则只认 SetAnchor 的固定矩形）
};

}} // namespace Maho::UI
