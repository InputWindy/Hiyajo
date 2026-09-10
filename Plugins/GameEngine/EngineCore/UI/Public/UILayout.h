#pragma once

#include "UITypes.h"

namespace Maho { namespace UI {

enum class EUIDirection : std::uint8_t
{
	Row,
	Column
};

/** Content = 量内容；Fixed = 绝对值；Fill = 父内容区减去 Margin；Fraction = 父内容区 * Value。 */
enum class EUISizeMode : std::uint8_t
{
	Content,
	Fixed,
	Fill,
	Fraction
};

struct FUILength
{
	EUISizeMode Mode = EUISizeMode::Content;
	float       Value = 0.f;

	static FUILength Content() { return { EUISizeMode::Content, 0.f }; }
	static FUILength Fixed(float V) { return { EUISizeMode::Fixed, V }; }
	static FUILength Fill() { return { EUISizeMode::Fill, 0.f }; }
	static FUILength Fraction(float F) { return { EUISizeMode::Fraction, F }; }
};

enum class EUIAlign : std::uint8_t
{
	Start,
	Center,
	End,
	Stretch
};

/** 节点布局参数。摆放算法只在 UILayoutEngine 里实现一次，节点不重写。 */
struct FUILayout
{
	EUIDirection Direction = EUIDirection::Column;
	FUILength    Width{};                                   // 本节点尺寸
	FUILength    Height{};
	FMargin      Margin{};                                  // 与本节点外部的间距
	FMargin      Padding{};                                 // 内容区内缩
	float        Spacing = 0.f;                             // 同级子项间距
	EUIAlign     HorizontalAlign = EUIAlign::Stretch;
	EUIAlign     VerticalAlign = EUIAlign::Start;
	bool         bClipChildren = false;                     // 子内容超出则裁剪

	FUILayout& SetDirection(EUIDirection D) { Direction = D; return *this; }
	FUILayout& SetSize(FUILength W, FUILength H) { Width = W; Height = H; return *this; }
	FUILayout& SetPadding(FMargin P) { Padding = P; return *this; }
	FUILayout& SetSpacing(float S) { Spacing = S; return *this; }
};

}} // namespace Maho::UI
