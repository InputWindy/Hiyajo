#pragma once

#include "UIApi.h"
#include "UIEvent.h"
#include "UIResource.h"
#include "UIStyle.h"
#include "UITypes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Maho { namespace UI {

class FUIView;
class FUIBuilder;

enum class EUITextAlign : std::uint8_t
{
	Left,
	Center,
	Right
};

enum class EUIScaleMode : std::uint8_t
{
	Stretch,
	Fit,
	Fill
};

/** 命中/裁剪标记（按位组合）。 */
enum class EUIInputFlags : std::uint32_t
{
	None       = 0,
	HitTest    = 1u << 0,   // 参与指针命中（按钮/输入等）
	Clip       = 1u << 1,   // 自身矩形作为子节点裁剪矩形
	Scroll     = 1u << 2,   // 允许滚动，内容超出时可滚动
	DragSource = 1u << 3,   // 可拖出
	DropTarget = 1u << 4,   // 可落入
	ContextMenu = 1u << 5   // 自身矩形是右键菜单区域（右键命中回写 `FUIWidgetState`，见 `HitTestSecondary`）
};

MAHO_UI_API EUIInputFlags operator|(EUIInputFlags A, EUIInputFlags B);
MAHO_UI_API EUIInputFlags operator&(EUIInputFlags A, EUIInputFlags B);
MAHO_UI_API bool HasFlag(EUIInputFlags V, EUIInputFlags F);

/** 翻译后端接口。公开是为了两件事：
 *    1) 换后端（当前唯一实现是 ImGui）；
 *    2) 记录式翻译器供布局/状态的自动化验证（无需 GPU）。
 *  公开头不含 imgui.h；ImGui 只出现在实现文件里。 */
class MAHO_UI_API IUITranslator
{
public:
	virtual ~IUITranslator() = default;

	/** 一帧翻译的开始/结束（后端可在此做帧簿记）。 */
	virtual void BeginView(FUIView& View, const FUIRect& DisplayRect) = 0;
	virtual void EndView(FUIView& View) = 0;

	/** 本帧视图显示区（局部坐标）—— 锚点比例（`FUIPanel::SetAnchor`）的参照。 */
	[[nodiscard]] virtual FUIRect GetDisplayRect() const = 0;

	/** 当前原点（局部坐标 → 屏幕坐标的偏移，随滚动区进栈变化）。
	 *  后端据此把节点矩形换算成屏幕矩形回写 `FUIWidgetState::ScreenRect`。 */
	[[nodiscard]] virtual FUIVector2 GetScreenOrigin() const = 0;

	/** 翻译线程入队一条交互事件 —— **只入队，不执行回调**（回调归所有者线程 `DrainEvents()`）。
	 *  组件用 `FUIBuilder::MakeEvent(Type)` 造记录（自带 Id 路径）。 */
	virtual void EnqueueEvent(FUIEventRecord Record) = 0;

	// -- 资源解析（FName 引用 -> 渲染资源；后端内部按引用缓存）-------------
	/** 字体资源：按 (Font, Size) 解析到字体图集条目。Font 为 None 时返回缺省字体。 */
	virtual FUIResolvedResource ResolveFont(FUIName Font, float Size) = 0;

	/** 纹理资源（图标 / 图片 / 渲染目标镜像）：先查渲染镜像池，未命中再问资源系统；
	 *  仍未就绪时 bValid=false，本帧按占位外观绘 —— 不阻塞帧、不做重试风暴。 */
	virtual FUIResolvedResource ResolveTexture(FUIName Texture) = 0;

	// -- 测量 --------------------------------------------------------------
	virtual FUIVector2 MeasureText(std::string_view Text, const FUIResolvedResource& Font,
								   float Size) = 0;
	virtual FUIVector2 MeasureIcon(const FUIResolvedResource& Icon, float Size) = 0;

	// -- 原生绘制基元 ------------------------------------------------------
	virtual void PushDisabled() = 0;      // 祖先禁用传播：后端映射 BeginDisabled
	virtual void PopDisabled() = 0;
	virtual void PushClip(const FUIRect& Rect) = 0;
	virtual void PopClip() = 0;
	virtual void DrawRect(const FUIRect& Rect, const FUIResolvedStyle& S) = 0;        // 填充+描边+圆角
	virtual void DrawText(const FUIRect& Rect, std::string_view Text,
						  const FUIResolvedResource& Font, float Size,
						  const FUIResolvedStyle& S, EUITextAlign Align) = 0;
	virtual void DrawIcon(const FUIRect& Rect, const FUIResolvedResource& Icon,
						  const FUIResolvedStyle& S) = 0;
	virtual void DrawImage(const FUIRect& Rect, const FUIResolvedResource& Texture,
						   const FUIColor& Tint, const FUIVector2& UV0, const FUIVector2& UV1) = 0;

	// -- 交互控件（语义等价 ImGui 同名控件；返回值即本帧交互结果）----------
	virtual FUIHitResult WidgetButton(FUIName Id, const FUIRect& Rect,
									  const FUIResolvedStyle& S) = 0;
	virtual FUIHitResult WidgetCheckbox(FUIName Id, const FUIRect& Rect, bool& bValue,
										const FUIResolvedStyle& S) = 0;
	virtual FUIHitResult WidgetSliderFloat(FUIName Id, const FUIRect& Rect, float& Value,
										   float Min, float Max, std::string_view Format,
										   const FUIResolvedStyle& S) = 0;
	virtual FUIHitResult WidgetInputText(FUIName Id, const FUIRect& Rect, std::string& Text,
										 std::string_view Hint, std::size_t MaxLength,
										 bool bMultiline, const FUIResolvedStyle& S) = 0;
	/** `bSelected` 只作为输入（语义等价 ImGui::Selectable 的 selected 形参）：选中态由树
	 *  （`FUIBuilder::SetSelected`）决定并已在样式里解析，后端不再回写。 */
	virtual FUIHitResult WidgetSelectable(FUIName Id, const FUIRect& Rect, bool bSelected,
										  const FUIResolvedStyle& S) = 0;
	/** 拖拽数值（1..4 分量，语义等价 ImGui::DragFloatN）：直接改写 `Values`。 */
	virtual FUIHitResult WidgetDragFloat(FUIName Id, const FUIRect& Rect, float* Values,
										 int Components, float Speed, std::string_view Format,
										 const FUIResolvedStyle& S) = 0;
	/** 颜色编辑（RGBA 四分量，语义等价 ImGui::ColorEdit4）：直接改写 `RGBA`。 */
	virtual FUIHitResult WidgetColorEdit(FUIName Id, const FUIRect& Rect, float* RGBA,
										 const FUIResolvedStyle& S) = 0;

	// -- 区域与容器辅助 ----------------------------------------------------
	/** 滚动区域请求（进区域前给）：`bToBottom` 一次性贴底，`bSetScrollY` 显式置量。 */
	struct FUIScrollRequest
	{
		bool  bToBottom = false;
		bool  bSetScrollY = false;
		float ScrollY = 0.f;
	};

	/** 出区域时实测的滚动量：写回节点的运行期状态（下一帧贴底判定用）。 */
	struct FUIScrollInfo
	{
		float ScrollY = 0.f;
		float ScrollMaxY = 0.f;
	};

	virtual bool BeginScrollRegion(FUIName Id, const FUIRect& Rect,
								   const FUIScrollRequest& Request) = 0;   // 返回内容是否可见
	virtual FUIScrollInfo EndScrollRegion() = 0;
	virtual FUIHitResult WidgetCollapsingHeader(FUIName Id, const FUIRect& Rect, bool& bOpen,
												const FUIResolvedStyle& S) = 0;

	/** 右键（次要键）**区域**命中：指针落在 `Rect` 内且本帧按下了右键。
	 *  纯几何判定、不走 item 命中 —— 滚动容器自己的 item 会被它的内容子窗口挡掉（子窗口在上），
	 *  而右键菜单要的恰是"整个矩形"这层含义。`OutPos` = 命中那一刻的指针位置（视图局部坐标），
	 *  可直接当弹层锚点（`FUIPopup` 把弹层左下角贴到锚点左上角 ⇒ 左下角即指针处）。 */
	[[nodiscard]] virtual bool HitTestSecondary(const FUIRect& Rect, FUIVector2& OutPos) = 0;

	// -- 弹出层（翻译器开第二窗口；面板零后端代码）--------------------------
	/** 悬停提示：返回 true 时后端已开提示窗口，调用方在窗口内摆子树后 `EndTooltip`。
	 *  `Anchor` 为锚点矩形（局部坐标），`bFollowMouse` 时锚点忽略。 */
	virtual bool BeginTooltip(FUIName Id, const FUIRect& Anchor, bool bFollowMouse) = 0;
	virtual void EndTooltip() = 0;

	/** 弹层锚点：**零尺寸矩形是合法的点锚点**（右键菜单：左下角落在指针处），故"有没有锚点"
	 *  单独用一位表达 —— 只看矩形是否为空就分不清"点锚点"和"没给锚点"。 */
	struct FUIPopupAnchor
	{
		bool   bHas = false;
		FUIRect Rect{};   // 局部坐标；落位用 `Rect.Min`（弹层左下角）与 `Rect.H`（放不下时翻到其下沿）
	};

	/** 弹层：返回 true 时后端已开始弹层，调用方摆子树后 `EndPopup()`。
	 *  `bOpen` 是树的期望状态；`bWasShown` 是**上一帧后端是否真的画出了它** —— 后端的跨帧记忆，
	 *  调用方持有（翻译器一帧一实例，记不住）：两者一起构成开合边沿，上升沿开、下降沿关；
	 *  用户自己关掉（点外部 / Esc）时"树要开但后端已关"，后端据 `bWasShown` 不开新窗、如实报 false，
	 *  调用方落回 `bOpen=false`。少了这份记忆就是每帧重新 `OpenPopup`：弹层反复被当成刚出现，
	 *  且"点外部关掉 -> 下一帧又弹"永不可关。`bModal` 映射模态弹层。
	 *  `ContentBox` 是调用方摆子树用的内容矩形（弹层自身**局部坐标**，含自身内边距的偏移）：
	 *  落位要用它决定"贴锚点下沿还是翻到上沿"—— 同样因为翻译器记不住上一帧尺寸，故由调用方
	 *  在开窗**之前**量好。注意高度要用 `ContentBox.Y + ContentBox.H`（内容底边在窗口内的
	 *  偏移），不是 `H`：ImGui 自适应窗口的高度 = **内容起点偏移 + 内容高** + 窗口内边距×2
	 *  （`CalcWindowAutoFitSize`），内容起点本身已在窗口内偏移了一个自身内边距 —— 少算这一段
	 *  就是弹层下沿压住锚点的那几像素。
	 *  `S` 是弹层自身样式：弹层内容是第二个窗口，后端取 `Normal.Fill` 当它的窗口底色
	 *  （调用方自己的绘制不覆盖那个窗口）。 */
	virtual bool BeginPopup(FUIName Id, bool bOpen, bool bWasShown, const FUIPopupAnchor& Anchor,
							bool bModal, const FUIRect& ContentBox, const FUIResolvedStyle& S) = 0;
	virtual void EndPopup() = 0;

	// -- 拖放（内容浏览器/资产拖拽复用同一套载荷）--------------------------
	virtual bool BeginDragSource(FUIName Id, std::string_view PayloadType,
								 std::string_view Payload, std::string_view PreviewText) = 0;
	virtual void EndDragSource() = 0;
	virtual bool IsDropTarget(FUIName Id, std::string_view PayloadType,
							  std::string* OutPayload) = 0;

	// -- 字体与光标 --------------------------------------------------------
	virtual void SetKeyboardFocus(FUIName Id) = 0;
	virtual bool HasFocus(FUIName Id) const = 0;

	/** 声明式键盘快捷键查询（翻译期由声明了快捷键的节点调用）：本帧命中该组合返回 true。
	 *  仅在键盘焦点落在本视图窗口（含子窗口）且当前无文本输入时命中 —— 否则打字会误触发。
	 *  命名键（`EUIKey`，如 ↑/↓）不受"无文本输入"那道守卫限制：它不产生字符、不参与文本输入，
	 *  而声明它的往往正是那个输入框自己（命令行 ↑ 翻历史），挡掉就永远不命中。 */
	virtual bool IsShortcutPressed(const FUIKeyChord& Chord) = 0;
	virtual void DebugDrawRect(const FUIRect& Rect, const FUIColor& C) = 0;
};

}} // namespace Maho::UI
