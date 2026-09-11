#pragma once

#include "UIApi.h"
#include "UITypes.h"

#include <Core/Delegate.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Maho { namespace UI {

/** 拖放载荷类型键（ImGui 的 payload type 字符串，须 ≤32 字符）。
 *  载荷内容是 `FUIName` 的字符串形式，故树内拖放跨后端可用同一键。 */
inline constexpr char kUIPayloadType[] = "MahoUIName";

class FUIBuilder;

enum class EUIEventType : std::uint8_t
{
	Clicked,
	ValueChanged,
	Toggled,
	TextChanged,
	SelectionChanged,
	DragStarted,
	DragDropped,
	TreeNodeToggled,
	PopupClosed,
	Submitted,           // 输入框回车提交
	Shortcut             // 声明式键盘快捷键命中（`FUIKeyChord`）
};

/** 修饰键（按位组合）：命中那一刻的键盘状态随事件一起送到所有者线程。
 *  多选（Shift/Ctrl 点选）等业务在回调里读 `FUIBuilder::GetLastModifiers()`。 */
enum class EUIModifiers : std::uint8_t
{
	None  = 0,
	Shift = 1u << 0,
	Ctrl  = 1u << 1,
	Alt   = 1u << 2
};

constexpr EUIModifiers operator|(EUIModifiers A, EUIModifiers B)
{
	return static_cast<EUIModifiers>(static_cast<std::uint8_t>(A) | static_cast<std::uint8_t>(B));
}

[[nodiscard]] constexpr bool HasModifier(EUIModifiers Set, EUIModifiers Flag)
{
	return (static_cast<std::uint8_t>(Set) & static_cast<std::uint8_t>(Flag)) != 0;
}

/** 命名键（无字符的键）：字符键走 `FUIKeyChord::Key`，↑/↓ 这类没有字符可表达的键走这里。
 *  只在后端有对应物理键时才有意义（v1 后端 = ImGui）。 */
enum class EUIKey : std::uint8_t
{
	None = 0,
	Up,
	Down
};

/** 命名键的显示名（快捷键事件的载荷 `FUIKeyChord::ToString` 用它）。 */
constexpr const char* EUIKeyName(EUIKey K)
{
	switch (K)
	{
	case EUIKey::Up:   return "Up";
	case EUIKey::Down: return "Down";
	case EUIKey::None: break;
	}
	return "";
}

/** 键盘快捷键：一个键 + 修饰键组合。字符键（A-Z / 0-9，ASCII，大小写等价）填 `Key`，
 *  无字符的键（↑/↓）填 `Named` —— 两者二选一。节点用 `FUIBuilder::OnShortcut` 声明；
 *  命中时入队 `EUIEventType::Shortcut`，记录里的 `Text` 即本结构的 `ToString()`，
 *  回调仍在所有者线程。 */
struct FUIKeyChord
{
	char         Key = '\0';
	EUIModifiers Mods = EUIModifiers::None;
	/** 命名键：只在 `Key == '\0'` 时生效。排最后是为了让既有的 `{ 'C', EUIModifiers::Ctrl }`
	 *  聚合初始化继续成立。 */
	EUIKey       Named = EUIKey::None;

	[[nodiscard]] bool IsNone() const { return Key == '\0' && Named == EUIKey::None; }

	/** 命名键的构造糖：`FUIKeyChord::NamedKey(EUIKey::Up)` —— 省掉两个空位。 */
	[[nodiscard]] static FUIKeyChord NamedKey(EUIKey In, EUIModifiers M = EUIModifiers::None)
	{
		FUIKeyChord Chord;
		Chord.Mods = M;
		Chord.Named = In;
		return Chord;
	}

	/** 显示名（"Ctrl+Shift+C" / "Up"）—— 同一节点可声明多条快捷键，事件靠它区分是哪一条。 */
	[[nodiscard]] std::string ToString() const
	{
		std::string Out;
		if (HasModifier(Mods, EUIModifiers::Ctrl))  { Out += "Ctrl+"; }
		if (HasModifier(Mods, EUIModifiers::Shift)) { Out += "Shift+"; }
		if (HasModifier(Mods, EUIModifiers::Alt))   { Out += "Alt+"; }
		if (Key != '\0')        { Out += Key; }
		else if (Named != EUIKey::None) { Out += EUIKeyName(Named); }
		return Out;
	}
};

/** 翻译线程 → 所有者线程 的一条事件。携带 Id 路径（根→目标）以消歧同名节点，
 *  **不含节点指针**，所有者线程按路径查树后回调（结构不变则路径稳定）。 */
struct FUIEventRecord
{
	FUIName              Target{};    // 目标节点 Id（= 路径末项）
	std::vector<FUIName> Path;        // 根 → 目标的 Id 链（不含根）
	EUIEventType         Type = EUIEventType::Clicked;
	FUIName              Payload{};     // 拖放载荷（DragDropped）
	float                Value = 0.f;   // 滑块值 / 数值载荷
	bool                 bFlag = false; // 复选/选中/展开
	EUIModifiers         Modifiers = EUIModifiers::None;   // 命中时的 Shift/Ctrl/Alt
	std::string          Text;          // 文本变更载荷
};

// 处理器签名（多播：同一事件可有多个订阅者）
using FUIEventHandler = std::function<void(FUIBuilder&)>;
using FUIFloatEventHandler = std::function<void(FUIBuilder&, float)>;
using FUIBoolEventHandler = std::function<void(FUIBuilder&, bool)>;
using FUITextEventHandler = std::function<void(FUIBuilder&, std::string_view)>;
using FUINameEventHandler = std::function<void(FUIBuilder&, FUIName)>;   // 拖放载荷

/** 节点事件集：**多播 Delegate**（Core 的 `TMulticastEvent`，header-only、线程安全）。
 *  翻译线程**从不**执行它们 —— 只入队 `FUIEventRecord`，由所有者在 `DrainEvents()` 里 `Broadcast`。
 *  懒分配（`FUIBuilder` 里是 `unique_ptr`）：没订阅就不付 mutex + vector 的钱；
 *  也因它内含 mutex（不可移动）而不能当节点的值成员。 */
struct FUIEvents
{
	TMulticastEvent<void(FUIBuilder&)>                   Clicked;          // 按钮 / 菜单项
	TMulticastEvent<void(FUIBuilder&, float)>            ValueChanged;     // 滑块
	TMulticastEvent<void(FUIBuilder&, bool)>             Toggled;          // 复选 / 折叠 / 树节点
	TMulticastEvent<void(FUIBuilder&, std::string_view)> TextChanged;      // 输入框
	TMulticastEvent<void(FUIBuilder&, std::string_view)> Submitted;        // 输入框回车提交
	TMulticastEvent<void(FUIBuilder&, bool)>             SelectionChanged; // 选中项
	TMulticastEvent<void(FUIBuilder&, FUIName)>          DragDropped;      // 落点的载荷
	TMulticastEvent<void(FUIBuilder&)>                   PopupClosed;      // FUIPopup 关闭
	TMulticastEvent<void(FUIBuilder&, std::string_view)> Shortcut;         // 键盘快捷键（载荷 = 组合名）
};

/** 订阅票据（Core `FSubscriptionID` 的别名，便于节点 API 读数）。 */
using FUIEventSubscription = FSubscriptionID;

}} // namespace Maho::UI
