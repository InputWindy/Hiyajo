#pragma once

#include "UIApi.h"
#include "UIEvent.h"
#include "UILayout.h"
#include "UIRender.h"
#include "UIStyle.h"
#include "UITypes.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Maho { namespace UI {

class IUITranslator;
struct FUIBlock;                 // 子节点声明块，定义在本头末尾

/** 运行期状态：翻译阶段回写、业务只读。与树结构字段分离，翻译器只写这里。 */
struct FUIWidgetState
{
	FUIRect    Rect{};              // 本节点视图局部矩形（帧外读到的是上一帧）
	FUIRect    ScreenRect{};        // 同一矩形的屏幕坐标（= 局部 + 视图原点）
	bool       bHovered  = false;
	bool       bPressed  = false;
	bool       bActive    = false;  // 被按下且指针仍在其上
	bool       bFocused  = false;
	bool       bSelected = false;
	/** 本帧在"右键菜单区域"（`EUIInputFlags::ContextMenu`）内按下了右键。
	 *  一次性：它表达的是"刚刚发生的一次右键"，业务在下一帧读它开菜单（与其余运行期状态同序）。 */
	bool       bSecondaryClicked = false;
	FUIVector2 PointerPos{};        // `bSecondaryClicked` 那一刻的指针位置（视图局部坐标，可直接当弹层锚点）
	EUIModifiers LastModifiers = EUIModifiers::None;   // 最近一次事件命中时的修饰键（回调里读）
	FUIVector2 ContentSize{};       // 本帧测量结果
	float      ScrollY    = 0.f;    // 滚动容器当前纵向滚动量
	float      ScrollMaxY = 0.f;    // 可滚动上限（贴底判定：ScrollY >= ScrollMaxY - 1）
};

/** 组件树节点基类。树是唯一数据源；每帧全量翻译到后端（v1 = ImGui）。 */
class MAHO_UI_API FUIBuilder
{
public:
	explicit FUIBuilder(FUIName InId);
	virtual ~FUIBuilder();

	FUIBuilder(const FUIBuilder&)            = delete;
	FUIBuilder& operator=(const FUIBuilder&) = delete;

	// 子节点是 unique_ptr（拷贝已删），虚析构又抑制隐式移动 —— 必须显式声明。
	// 只可用于尚未挂载的新节点（无父无子）：已挂载节点一旦移动，其子节点的 Parent 会悬垂。
	FUIBuilder(FUIBuilder&&) noexcept            = default;
	FUIBuilder& operator=(FUIBuilder&&) noexcept = default;

	// -- 身份与树 -----------------------------------------------------------
	[[nodiscard]] FUIName          GetId() const { return Id; }
	[[nodiscard]] std::string_view GetTypeName() const { return TypeName(); }
	[[nodiscard]] FUIBuilder*      GetParent() const { return Parent; }
	[[nodiscard]] const std::vector<std::unique_ptr<FUIBuilder>>& GetChildren() const
	{ return Children; }

	/** 声明一个子节点并返回其引用。
	 *  - 同 Id + 同类型 = 复用已有节点（保留运行期状态），不新建
	 *  - 同 Id + 不同类型 = 原地替换该子节点（其余子节点不动）
	 *  - Id 为 None 时不可复用（每次新建），诊断由调用点断言负责 */
	template <typename T, typename... TArgs>
	T& AddItem(FUIName InId, TArgs&&... Args)
	{
		static_assert(std::is_base_of_v<FUIBuilder, T>,
			"AddItem<T>: T must derive from Maho::UI::FUIBuilder");
		static_assert(std::is_constructible_v<T, FUIName, TArgs...>,
			"AddItem<T>: T must be constructible from (FUIName, TArgs...)");
		if (FUIBuilder* Existing = FindChild(InId))
		{
			if (auto* Typed = dynamic_cast<T*>(Existing)) { return *Typed; }
			RemoveItem(InId);
		}
		auto Child = std::make_unique<T>(InId, std::forward<TArgs>(Args)...);
		T& Ref = *Child;
		Child->Parent = this;
		Child->Serial = AllocateSerial();
		Children.push_back(std::move(Child));
		return Ref;
	}

	void        ResetChildren();
	bool        RemoveItem(FUIName InId);
	FUIBuilder* FindChild(FUIName InId) const;

	/** 声明式子节点块：块内集合 = 本节点子节点的**全量**（同 Id 同类型复用、同 Id 异类型替换、
	 *  未声明者移除、顺序按声明顺序）。返回 `*this`，故类型专属 setter 要写在 `[]` 之前。 */
	FUIBuilder& operator[](FUIBlock InBlock);

	// -- 布局 / 样式 / 状态 -------------------------------------------------
	FUILayout&       Layout() { return LayoutParams; }
	const FUILayout& GetLayout() const { return LayoutParams; }
	FUIStyle&        Style() { return StyleOverride; }
	const FUIStyle&  GetStyle() const { return StyleOverride; }
	const FUIResolvedStyle& GetResolvedStyle() const { return ResolvedStyle; }

	/** 该类型（编译期类型）的静态默认样式。 */
	[[nodiscard]] const FUIStyle& GetTypeDefaultStyle() const { return TypeDefaultStyle(); }

	FUIBuilder& SetDisabled(bool bIn) { bDisabled = bIn; return *this; }
	FUIBuilder& SetVisible(bool bIn) { bVisible = bIn; return *this; }
	FUIBuilder& SetSelected(bool bIn) { bSelected = bIn; return *this; }
	/** 选中态：样式解析（`EUIState::Selected`）用的**唯一**来源。组件不得再自存一份
	 *  （先例：`FUISelectable` 曾自带镜像成员，导致只传给后端、样式不换色）。 */
	[[nodiscard]] bool IsSelected() const { return bSelected; }

	[[nodiscard]] bool IsDisabled() const;   // 自身或任一祖先禁用
	[[nodiscard]] bool IsVisible() const;    // 自身或任一祖先隐藏
	[[nodiscard]] EUIState GetVisualState() const;
	[[nodiscard]] const FUIWidgetState& GetState() const { return State; }
	[[nodiscard]] const FUIRect& GetRect() const { return State.Rect; }
	/** 屏幕坐标矩形（上一帧）。视图局部 → 屏幕的换算由后端给，业务不需要知道窗口偏移。 */
	[[nodiscard]] const FUIRect& GetScreenRect() const { return State.ScreenRect; }
	/** 最近一次事件命中时的修饰键（Shift/Ctrl/Alt）：仅在事件回调里读才有意义。 */
	[[nodiscard]] EUIModifiers GetLastModifiers() const { return State.LastModifiers; }

	/** 运行期状态写入口 —— **翻译器专用**；业务只读 `GetState()`。 */
	FUIWidgetState& MutableState() { return State; }

	// -- 事件（多播 Delegate）----------------------------------------------
	/** 懒分配：无订阅时为 nullptr。内含 mutex 故不可移动，只能是节点上的指针成员。 */
	FUIEvents* GetEvents() { return Events.get(); }
	FUIEvents* GetEvents() const { return Events.get(); }

	/** 订阅（仅在所有者线程的 `Edit()` 区间内调用）；返回票据供 Unbind。 */
	FUIEventSubscription BindClick(FUIEventHandler H);
	FUIEventSubscription BindValueChanged(FUIFloatEventHandler H);
	FUIEventSubscription BindToggled(FUIBoolEventHandler H);
	FUIEventSubscription BindTextChanged(FUITextEventHandler H);
	FUIEventSubscription BindSubmitted(FUITextEventHandler H);
	FUIEventSubscription BindSelectionChanged(FUIBoolEventHandler H);
	FUIEventSubscription BindDragDropped(FUINameEventHandler H);
	FUIEventSubscription BindPopupClosed(FUIEventHandler H);
	/** 键盘快捷键（`EUIEventType::Shortcut`）：订阅**这一条 chord** 的命中。事件按 chord 分组
	 *  派发，回调不带载荷（组本身就是身份）。作用域在声明侧（`OnShortcut`）给出。 */
	FUIEventSubscription BindShortcut(FUIKeyChord Chord, FUIEventHandler H);
	void UnbindClick(FUIEventSubscription Id);
	void UnbindValueChanged(FUIEventSubscription Id);
	void UnbindToggled(FUIEventSubscription Id);
	void UnbindTextChanged(FUIEventSubscription Id);
	void UnbindSubmitted(FUIEventSubscription Id);
	void UnbindSelectionChanged(FUIEventSubscription Id);
	void UnbindDragDropped(FUIEventSubscription Id);
	void UnbindPopupClosed(FUIEventSubscription Id);
	/** 注销 `BindShortcut` 的订阅（需要给出当时声明的 chord：订阅组按 chord 分）。 */
	void UnbindShortcut(FUIKeyChord Chord, FUIEventSubscription Id);

	/** 糖：订阅 + 返回 `*this`（链式）。注销请用上面的 BindXxx 取票据。 */
	FUIBuilder& OnClick(FUIEventHandler H);
	FUIBuilder& OnValueChanged(FUIFloatEventHandler H);
	FUIBuilder& OnToggled(FUIBoolEventHandler H);
	FUIBuilder& OnTextChanged(FUITextEventHandler H);
	FUIBuilder& OnSubmitted(FUITextEventHandler H);
	FUIBuilder& OnSelectionChanged(FUIBoolEventHandler H);
	FUIBuilder& OnDragDropped(FUINameEventHandler H);
	FUIBuilder& OnPopupClosed(FUIEventHandler H);

	/** 键盘快捷键：声明"本节点关心的组合 + 作用域 + 命中回调"。
	 *  翻译期（本节点参与翻译且未禁用时）按声明序逐条查询后端，命中即入队 `EUIEventType::Shortcut`；
	 *  所有者线程只把它派发给**这一条 chord** 的订阅者（按 chord 分组，同一节点多条快捷键互不串台）。
	 *  作用域默认 `NodeActive` = 本节点自己正拿着输入才匹配（"输入框没光标就别响应箭头键"因此是
	 *  引擎语义，面板不必自写闸门）；容器级/面板级快捷键显式传 `Anywhere`。
	 *  快捷键的**可见性由本节点决定** —— 声明了就有，没声明就没有（不需要全局注册表）。
	 *  只应声明一次，逐帧重来会累积订阅（与其它 `OnXxx` 同约定，放在 `if (bNew)` 里）。 */
	FUIBuilder& OnShortcut(FUIKeyChord Chord, FUIEventHandler H,
						   EUIShortcutScope Scope = EUIShortcutScope::NodeActive);

	/** 翻译器改用：把一条事件在本节点上 Broadcast（所有者线程调用）。 */
	void BroadcastEvent(const FUIEventRecord& Record);

	/** 造一条指向本节点的事件记录（自带根→本节点的 Id 路径）。翻译线程用，
	 *  随后交给 `IUITranslator::EnqueueEvent` 入队 —— 不做回调。 */
	[[nodiscard]] FUIEventRecord MakeEvent(EUIEventType Type) const;

	// -- 滚动 ---------------------------------------------------------------
	/** 请求下一次翻译把滚动条贴底（Console 自动滚动）；一次性，翻译后自动清除。 */
	FUIBuilder& RequestScrollToBottom();
	/** 置滚动量（像素）。跨线程下等价于一条请求，由翻译线程在下一次翻译时应用。 */
	FUIBuilder& SetScrollY(float InScrollY);
	[[nodiscard]] float GetScrollY() const { return State.ScrollY; }
	[[nodiscard]] float GetScrollMaxY() const { return State.ScrollMaxY; }

	/** 翻译器专用：取走一次性滚动请求；无请求返回 false。 */
	bool ConsumeScrollRequest(bool& bOutToBottom, float& OutScrollY);

	// -- 键盘焦点 -----------------------------------------------------------
	/** 请求下一次翻译把键盘焦点交给本节点（输入框自动补全连续性）。
	 *  一次性，翻译后自动清除；跨线程下等价于一条请求。 */
	FUIBuilder& RequestKeyboardFocus();
	/** 翻译器专用：取走一次性焦点请求；无请求返回 false。 */
	bool ConsumeFocusRequest();

	// -- 拖放 ---------------------------------------------------------------
	/** 声明本节点可被拖动，载荷是一个 `FUIName`（资产引用）。None = 不是拖放源。 */
	FUIBuilder& SetDragSource(FUIName Payload);
	[[nodiscard]] FUIName GetDragSource() const { return DragPayload; }

	/** 声明本节点可落入：落点事件走 `FUIEvents::DragDropped`（多播）。 */
	FUIBuilder& OnDropTarget(FUINameEventHandler H);
	[[nodiscard]] bool IsDropTarget() const { return bIsDropTarget; }

	// -- 翻译（翻译器驱动；业务不直接调用）---------------------------------
	void Translate(IUITranslator& T, const FUIRect& InRect);

protected:
	// -- 子类扩展点 --------------------------------------------------------
	virtual std::string_view TypeName() const = 0;

	/** 内容尺寸（叶子：文本/图标经翻译器测量）。容器默认返回子节点累加值。 */
	virtual FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const;

	/** 本帧矩形的解析入口：默认按 Layout（FrameRect）；`FUIPanel` 的锚点比例覆盖它。 */
	[[nodiscard]] virtual FUIRect ResolveFrame(const FUIRect& Allocated, IUITranslator& T) const;

	virtual void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) {}
	virtual void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) {}

	/** 子节点摆放；默认按 LayoutParams 交给布局引擎。FUIGrid/FUICollapsingHeader 覆盖它。 */
	virtual void ArrangeChildren(IUITranslator& T);

	/** 覆盖在子节点之上（滚动条、遮罩、浮动菜单）。 */
	virtual void PaintOverlay(IUITranslator& T, const FUIResolvedStyle& S) {}

	/** 每类型静态默认样式：定义在**该类型自己的 cpp**（键函数落在唯一模块）。 */
	virtual const FUIStyle& TypeDefaultStyle() const = 0;

	/** 浮层节点（提示 / 弹层）：正常流里不占位（矩形可为空），内容画在第二个窗口。
	 *  此类节点矩形为空时仍会调用 `PaintContent`（其余节点直接跳过本帧）。 */
	virtual bool IsOverlayLayer() const { return false; }

	/** 块声明复用时的类型专属配置同步：把**声明节点**上的结构化字段（Label/Value/…）搬到
	 *  被复用的既有节点上（结构/样式/事件由 `operator[]` 统一处理，运行期状态保留旧值）。 */
	virtual void SyncConfig(const FUIBuilder& Declared) { (void)Declared; }

	/** 参与命中测试 / 裁剪 / 滚动的标记位。 */
	virtual EUIInputFlags GetInputFlags() const;

	virtual bool WantsKeyboardFocus() const { return false; }

private:
	friend class FUIView;
	friend struct FUILayoutEngine;
	friend struct FUIStyleResolver;

	/** 拖放通道（翻译期调用）：声明载荷 / 接受落点，落点事件只入队。 */
	void TranslateDragDrop(IUITranslator& T);

	std::uint32_t AllocateSerial();   // 视图级只增序号（自本节点上溯到根取计数器）

	FUIEvents& EnsureEvents();        // 懒分配

	FUIName       Id;
	FUIBuilder*   Parent   = nullptr;
	std::uint32_t Serial   = 0;
	std::uint32_t NextSerial = 1;     // 仅根节点使用

	std::vector<std::unique_ptr<FUIBuilder>> Children;

	FUILayout        LayoutParams;
	FUIStyle         StyleOverride;
	FUIResolvedStyle ResolvedStyle;                 // 每帧解析结果缓存
	FUIWidgetState   State;                         // 翻译期回写
	mutable std::unique_ptr<FUIEvents> Events;      // 懒分配的多播事件集

	FUIName DragPayload{};
	std::vector<FUIShortcutDecl> Shortcuts;   // 声明式快捷键（声明序 = 查询序）
	bool    bDisabled = false;
	bool    bVisible  = true;
	bool    bSelected = false;
	bool    bIsDropTarget = false;       // OnDropTarget 声明过
	bool    bPendingToBottom = false;   // 一次性：下次翻译贴底
	bool    bPendingScrollY  = false;   // 一次性：下次翻译置滚动量
	float   PendingScrollY   = 0.f;
	bool    bPendingFocus    = false;   // 一次性：下次翻译取键盘焦点
};

/** §16 声明式块：`Root["panel"][ FUIText{"title"}, FUIButton{"ok"} ]`。
 *  刻意不提供 `initializer_list` 构造 —— 避免 `{...}` 歧义，强制变参构造。 */
struct FUIBlock
{
	template <typename... TKids>
	FUIBlock(TKids&&... Kids)
	{
		Nodes.reserve(sizeof...(TKids));
		(Nodes.push_back(std::make_unique<std::remove_reference_t<TKids>>(std::move(Kids))), ...);
	}

	std::vector<std::unique_ptr<FUIBuilder>> Nodes;
};

/** 块内节点工厂：按值返回（拷贝已删，靠移动），Id 必填。 */
template <typename T, typename... TArgs>
[[nodiscard]] T AddItem(FUIName InId, TArgs&&... Args)
{
	static_assert(std::is_base_of_v<FUIBuilder, T>,
		"UI::AddItem<T>: T must derive from Maho::UI::FUIBuilder");
	return T(InId, std::forward<TArgs>(Args)...);
}

}} // namespace Maho::UI
