#pragma once

// 唯一允许 include <imgui.h> 的原语边界（本头只在 Private/ 内被包含）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <UIRender.h>
#include <UIView.h>

#include "imgui.h"

#include <unordered_map>
#include <vector>

namespace Maho { namespace UI {

/** `IUITranslator` 的 ImGui 实现：把树的语义原语映射为 ImGui 绘制/控件调用。
 *  一处翻译器实例 = 一次 `TranslateView`（或叠加层一帧），不跨帧复用。 */
class FImGuiTranslator final : public IUITranslator
{
public:
	explicit FImGuiTranslator(ImGuiContext* InContext) : Context(InContext) {}

	// -- IUITranslator --
	void BeginView(FUIView& View, const FUIRect& DisplayRect) override;
	void EndView(FUIView& View) override;
	[[nodiscard]] FUIRect GetDisplayRect() const override { return Display; }
	[[nodiscard]] FUIVector2 GetScreenOrigin() const override { return Origin; }
	void EnqueueEvent(FUIEventRecord Record) override;

	FUIResolvedResource ResolveFont(FUIName Font, float Size) override;
	FUIResolvedResource ResolveTexture(FUIName Texture) override;

	FUIVector2 MeasureText(std::string_view Text, const FUIResolvedResource& Font, float Size) override;
	FUIVector2 MeasureIcon(const FUIResolvedResource& Icon, float Size) override;

	void PushDisabled() override;
	void PopDisabled() override;
	void PushClip(const FUIRect& Rect) override;
	void PopClip() override;
	void DrawRect(const FUIRect& Rect, const FUIResolvedStyle& S) override;
	void DrawText(const FUIRect& Rect, std::string_view Text, const FUIResolvedResource& Font,
				  float Size, const FUIResolvedStyle& S, EUITextAlign Align) override;
	void DrawIcon(const FUIRect& Rect, const FUIResolvedResource& Icon, const FUIResolvedStyle& S) override;
	void DrawImage(const FUIRect& Rect, const FUIResolvedResource& Texture, const FUIColor& Tint,
				   const FUIVector2& UV0, const FUIVector2& UV1) override;

	FUIHitResult WidgetButton(FUIName Id, const FUIRect& Rect, const FUIResolvedStyle& S) override;
	FUIHitResult WidgetCheckbox(FUIName Id, const FUIRect& Rect, bool& bValue,
								const FUIResolvedStyle& S) override;
	FUIHitResult WidgetSliderFloat(FUIName Id, const FUIRect& Rect, float& Value, float Min, float Max,
								   std::string_view Format, const FUIResolvedStyle& S) override;
	FUIHitResult WidgetInputText(FUIName Id, const FUIRect& Rect, std::string& Text,
								 std::string_view Hint, std::size_t MaxLength, bool bMultiline,
								 const FUIResolvedStyle& S) override;
	FUIHitResult WidgetSelectable(FUIName Id, const FUIRect& Rect, bool bSelected,
								  const FUIResolvedStyle& S) override;
	FUIHitResult WidgetDragFloat(FUIName Id, const FUIRect& Rect, float* Values, int Components,
								 float Speed, std::string_view Format, const FUIResolvedStyle& S) override;
	FUIHitResult WidgetColorEdit(FUIName Id, const FUIRect& Rect, float* RGBA,
								 const FUIResolvedStyle& S) override;

	bool BeginScrollRegion(FUIName Id, const FUIRect& Rect, const FUIScrollRequest& Request) override;
	FUIScrollInfo EndScrollRegion() override;
	FUIHitResult WidgetCollapsingHeader(FUIName Id, const FUIRect& Rect, bool& bOpen,
										const FUIResolvedStyle& S) override;
	bool HitTestSecondary(const FUIRect& Rect, FUIVector2& OutPos) override;

	bool BeginTooltip(FUIName Id, const FUIRect& Anchor, bool bFollowMouse) override;
	void EndTooltip() override;
	bool BeginPopup(FUIName Id, bool bOpen, bool bWasShown, const FUIPopupAnchor& Anchor, bool bModal,
					const FUIRect& ContentBox, const FUIResolvedStyle& S) override;
	void EndPopup() override;

	bool BeginDragSource(FUIName Id, std::string_view PayloadType, std::string_view Payload,
						 std::string_view PreviewText) override;
	void EndDragSource() override;
	bool IsDropTarget(FUIName Id, std::string_view PayloadType, std::string* OutPayload) override;

	void SetKeyboardFocus(FUIName Id) override;
	bool HasFocus(FUIName Id) const override;
	bool IsShortcutPressed(const FUIKeyChord& Chord) override;
	void DebugDrawRect(const FUIRect& Rect, const FUIColor& C) override;

	// -- 本后端专用（宿主入口用）-------------------------------------------
	/** 调试叠加：命中项描红框（帧描述 `bDrawDebug` 透传）。 */
	bool bDebugDraw = false;

	/** 局部坐标 → 屏幕坐标（编辑器面板的窗口偏移）。 */
	[[nodiscard]] FUIRect ToScreen(const FUIRect& Local) const;
	[[nodiscard]] FUIView* CurrentView() const { return View; }

private:
	[[nodiscard]] ImVec2            ScreenMin(const FUIRect& Local) const;
	[[nodiscard]] ImVec2            ScreenMax(const FUIRect& Local) const;
	[[nodiscard]] static ImU32      ToColor(const FUIColor& C);
	[[nodiscard]] ImFont*           FontOf(const FUIResolvedResource& Font, float Size) const;
	// `bAllowHoverWhileActive`：同窗口另有活跃项时仍报告悬停位。ImGui 默认把「非活跃项」的
	// `IsItemHovered()` 直接压成 false（`IsWindowContentHoverable` 的同窗口活跃项过滤），
	// 于是"拖着 A 扫过 B"里 B 永远不报悬停。拖拽扩选（控制台日志行）正需要 B 的悬停位。
	[[nodiscard]] FUIHitResult      HitTestItem(FUIName Id, const FUIRect& Local, bool bHitTest,
	                                            bool bAllowHoverWhileActive = false);

	ImGuiContext* Context = nullptr;
	FUIView*      View = nullptr;

	FUIVector2 Origin{};             // 当前坐标原点（屏幕坐标）
	FUIRect    Display{};            // 本帧显示区（局部坐标）
	std::vector<FUIVector2> OriginStack;

	FUIResolvedResource DefaultFont;
	std::unordered_map<std::uint32_t, FUIResolvedResource> TextureCache;   // 每帧清空
	std::unordered_map<std::uint32_t, FUIResolvedResource> FontCache;

	std::uint32_t PendingFocusId = 0;
	std::uint32_t FocusedId = 0;
	int           DisabledDepth = 0;
	bool          bPopupOpen = false;                     // 当前是否已在弹层窗口内
	bool          bPopupBgPushed = false;                 // 弹层窗口底色的样式压栈配平（开窗失败也要弹掉）
	std::vector<FUIScrollRequest> ScrollStack;            // 滚动区域的待应用请求（贴底要等内容摆完）
};

/** 关掉 `View` 的树锁之前用的内部入口（锁在宿主入口里取）。 */
void TranslateViewBody(FUIView& View, FImGuiTranslator& T, const FUIRect& LocalRect);

}} // namespace Maho::UI
