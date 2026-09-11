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
	/** 内容测量宽度（弹层窗口自适应尺寸的宽度基准，< 0 = 用默认值）。内容最终是
	 *  "最宽子节点 + 自身内边距"，故窗口宽度 ≈ 本值 + 内边距 —— 想定住窗口宽度就把
	 *  内边距那一段一起算进来。 */
	FUIPopup& SetMeasureWidth(float InWidth) { MeasureWidth = InWidth; return *this; }
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
	/** 弹层落位目标（锚点或父节点矩形；局部坐标；可能没锚点 —— 见 `IUITranslator::FUIPopupAnchor`）。 */
	[[nodiscard]] IUITranslator::FUIPopupAnchor ResolveAnchor() const;

	FUIRect Anchor{};
	bool    bOpen = false;
	bool    bModal = false;
	bool    bHasAnchor = false;
	bool    bFollowAnchor = true;   // 每帧跟随父节点矩形（否则只认 SetAnchor 的固定矩形）
	float   MeasureWidth = -1.f;    // 内容测量宽度，< 0 = 默认（`kPopupMeasureWidth`）

	/** 上一帧后端**真的**画出了这个弹层吗（后端的跨帧记忆，翻译器一帧一实例记不住，故寄存在
	 *  节点上）：开合边沿要用它 —— 上升沿（树要开、后端没画）才开新窗；用户自己关掉的那一帧
	 *  "树还要开、后端已经没画"，据此不开新窗，交给调用方落回 `bOpen=false`。
	 *  **不进 `SyncConfig`**：这是后端的实测事实，不是声明侧能改写的东西。 */
	bool    bShownLastFrame = false;
};

}} // namespace Maho::UI
