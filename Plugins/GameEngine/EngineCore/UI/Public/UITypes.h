#pragma once

#include "UIApi.h"
#include <Name.h>          // FUIName = Name::FName（O(1) 比较 / 池化）
#include <cstddef>
#include <cstdint>

namespace Maho { namespace UI {

/** 组件 Id：复用 Name 插件的内化字符串池。同级唯一；跨级可重名（事件路由走路径）。 */
using FUIName = Name::FName;

struct FUIVector2
{
	float X = 0.f;
	float Y = 0.f;
};

struct FUIRect
{
	float X = 0.f, Y = 0.f, W = 0.f, H = 0.f;

	[[nodiscard]] FUIVector2 Min() const { return { X, Y }; }
	[[nodiscard]] FUIVector2 Max() const { return { X + W, Y + H }; }
	[[nodiscard]] bool Contains(FUIVector2 P) const;
	[[nodiscard]] FUIRect Intersect(const FUIRect& O) const;
	[[nodiscard]] bool IsEmpty() const { return W <= 0.f || H <= 0.f; }
};

struct FMargin
{
	float Left = 0.f, Top = 0.f, Right = 0.f, Bottom = 0.f;

	FMargin() = default;
	FMargin(float All) : Left(All), Top(All), Right(All), Bottom(All) {}
	FMargin(float H, float V) : Left(H), Top(V), Right(H), Bottom(V) {}
	FMargin(float L, float T, float R, float B) : Left(L), Top(T), Right(R), Bottom(B) {}
};

/** 组件**类型**（"这是哪一个组件"的那条轴）。
 *
 *  身份由组件自己声明：每个具体组件的构造函数把它交给 `FUIBuilder`（构造函数的**必填**首参，
 *  漏写 = 编译错误），于是"我是什么类型"是对象自己给出的，而不是靠 RTTI 反推。
 *  消费者因此可以 `GetType() == EUIViewType::X` 判身份 + `static_cast` 取类型 —— 对象自己
 *  声明过身份，这个转换就是可靠的，不必再用 `dynamic_cast`。
 *  新增组件类型 = 在此加一个值，并让该组件的构造函数传它。 */
enum class EUIViewType : std::uint8_t
{
	Button,
	Text,
	Image,
	Separator,
	InputText,
	Checkbox,
	SliderFloat,
	DragFloat,
	ColorEdit,
	Selectable,
	CollapsingHeader,
	TreeNode,
	Panel,
	Popup,
	Tooltip,
	Box,
	Grid,
	Canvas,
	None            // 无身份：`FUIBuilder` 抽象基类自身（不直接实例化）
};

/** 五态：常态 / 悬停 / 按下 / 选中 / 禁用。数组下标即 EUIState。 */
enum class EUIState : std::uint8_t
{
	Normal,
	Hovered,
	Pressed,
	Selected,
	Disabled,
	Count
};

constexpr std::size_t kUIStateCount = static_cast<std::size_t>(EUIState::Count);

/** 命中结果：翻译阶段把交互意图写回节点，回调由所有者线程抽干执行。 */
struct FUIHitResult
{
	bool bHovered = false;
	bool bPressed = false;
	bool bClicked = false;   // 本帧完成一次点击
	bool bSubmitted = false; // 输入框本帧回车提交
	bool bDragging = false;
	bool bReleased = false;
};

}} // namespace Maho::UI
