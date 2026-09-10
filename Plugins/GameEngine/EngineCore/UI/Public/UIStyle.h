#pragma once

#include "UITypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Maho { namespace UI {

struct FUIColor
{
	float R = 1.f, G = 1.f, B = 1.f, A = 1.f;

	/** 0xRRGGBB。 */
	static FUIColor Hex(std::uint32_t RGB, float InA = 1.f);
	[[nodiscard]] FUIColor WithAlpha(float InA) const;
	[[nodiscard]] FUIColor Lerp(const FUIColor& O, float T) const;

	[[nodiscard]] bool operator==(const FUIColor& O) const
	{
		return R == O.R && G == O.G && B == O.B && A == O.A;
	}
};

/** 一个状态组里可覆盖的样式项。未置位 = 交给回退链的下一级。
 *  颜色/尺寸是**字面量**，字体/图标是**资源引用**（FUIName），两者走同一条回退链。 */
struct FUIStateStyle
{
	std::optional<FUIColor> Fill;        // 背景填充
	std::optional<FUIColor> Stroke;      // 描边
	std::optional<FUIColor> Text;        // 文本/前景
	std::optional<float>    StrokeWidth;
	std::optional<float>    Radius;      // 圆角
	std::optional<float>    FontSize;
	std::optional<FMargin>  Padding;     // 内边距
	std::optional<FUIName>  Font;        // 字体资源引用 —— 可逐状态换（如禁用态换灰体）
	std::optional<FUIName>  Icon;        // 图标资源引用 —— 可逐状态换（如选中态换实心图标）
	std::optional<float>    IconSize;    // 图标边长（Content 尺寸时参与测量）
};

/** 实例样式：五个状态组 + 四态共用的字体/图标快捷槽。字段全 optional。 */
struct FUIStyle
{
	std::array<FUIStateStyle, kUIStateCount> States;
	std::optional<FUIName> Font;   // == States[0..4].Font 的便捷写法（只覆盖 Normal 之外的空项）
	std::optional<FUIName> Icon;

	FUIStateStyle&       operator[](EUIState S) { return States[static_cast<std::size_t>(S)]; }
	const FUIStateStyle& operator[](EUIState S) const { return States[static_cast<std::size_t>(S)]; }

	void SetFillAll(FUIColor C);          // 五态同色
	void SetTextAll(FUIColor C);
	void SetPaddingAll(FMargin M);
	void SetFontAll(FUIName F);           // 五态同字体
	void SetIconAll(FUIName I);
	[[nodiscard]] bool IsEmpty() const;   // 无任何覆盖
};

/** 解析结果：纯值（资源项为 FName 引用），翻译期直接消费（每帧每节点算一次）。
 *  Font/Icon 只解析到"引用"，真正的渲染资源由翻译后端按引用去解析。 */
struct FUIResolvedStyle
{
	FUIColor Fill{ 0.f, 0.f, 0.f, 0.f };
	FUIColor Stroke{ 0.f, 0.f, 0.f, 0.f };
	FUIColor Text{ 1.f, 1.f, 1.f, 1.f };
	float    StrokeWidth = 0.f;
	float    Radius = 0.f;
	float    FontSize = 14.f;
	FMargin  Padding{};
	FUIName  Font{};                      // 字体资源引用（None = 后端缺省字体）
	FUIName  Icon{};                      // 图标资源引用（None = 无图标）
	float    IconSize = 16.f;
	EUIState State = EUIState::Normal;    // 本节点本帧生效的状态组（诊断/调试绘制用）
};

}} // namespace Maho::UI
