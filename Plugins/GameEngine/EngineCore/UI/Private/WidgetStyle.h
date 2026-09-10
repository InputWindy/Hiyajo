#pragma once

// 组件实现共用的小工具（只在 Private/ 内使用，不对外暴露）。
#include <FUIBuilder.h>
#include <UITheme.h>

#include <utility>

namespace Maho { namespace UI { namespace Detail {

/** 类型默认样式的静态缓存：**主题换代后自动重建**。
 *  组件的 `TypeDefaultStyle()` 返回引用，故必须落在某个静态对象上；
 *  若只建一次，主题 token 改写后旧颜色会永久留着。 */
struct FTypeStyleCache
{
	std::uint32_t Stamp = 0xFFFFFFFFu;
	FUIStyle      Style;

	template <typename TBuild>
	const FUIStyle& Get(TBuild&& Build)
	{
		const std::uint32_t Now = GetUIThemeStamp();
		if (Stamp != Now)
		{
			Style = FUIStyle{};
			Build(Style);
			Stamp = Now;
		}
		return Style;
	}
};

/** 本帧矩形去掉内边距 = 内容区（节点自己的 Padding 优先于样式的 Padding）。 */
inline FUIRect ContentRectOf(const FUIBuilder& Node)
{
	FMargin Pad = Node.GetLayout().Padding;
	if (Pad.Left == 0.f && Pad.Top == 0.f && Pad.Right == 0.f && Pad.Bottom == 0.f)
	{
		Pad = Node.GetResolvedStyle().Padding;
	}
	const FUIRect R = Node.GetRect();
	return FUIRect{ R.X + Pad.Left, R.Y + Pad.Top,
					R.W - Pad.Left - Pad.Right, R.H - Pad.Top - Pad.Bottom };
}

inline FUIRect Inflate(const FUIRect& R, float X, float Y)
{
	return FUIRect{ R.X - X, R.Y - Y, R.W + X * 2.f, R.H + Y * 2.f };
}

/** 命中结果回写运行期状态（业务只读 `GetState()`；结构化字段仍在节点上）。 */
inline void WriteHit(FUIBuilder& Node, const FUIHitResult& Hit)
{
	FUIWidgetState& State = Node.MutableState();
	State.bHovered = Hit.bHovered;
	State.bPressed = Hit.bPressed;
	State.bActive  = Hit.bPressed;
	State.bFocused = Hit.bHovered && Hit.bPressed;
}

/** 事件入队（翻译线程只入队；回调归所有者 `FUIView::DrainEvents()`）。 */
inline void Enqueue(FUIBuilder& Node, IUITranslator& T, EUIEventType Type,
					float Value = 0.f, bool bFlag = false, std::string Text = std::string())
{
	if (Node.IsDisabled()) { return; }   // 禁用节点不产生事件
	FUIEventRecord Record = Node.MakeEvent(Type);
	Record.Value = Value;
	Record.bFlag = bFlag;
	Record.Text = std::move(Text);
	T.EnqueueEvent(std::move(Record));
}

/** 通用可点控件：跑后端命中 → 回写状态 → 完成点击时入队 `Clicked`。 */
inline FUIHitResult HitAndReport(FUIBuilder& Node, IUITranslator& T, bool bDrawChrome)
{
	const FUIHitResult Hit = bDrawChrome
		? T.WidgetButton(Node.GetId(), Node.GetRect(), Node.GetResolvedStyle())
		: T.WidgetButton(Node.GetId(), Node.GetRect(), FUIResolvedStyle{});
	WriteHit(Node, Hit);
	if (Hit.bClicked) { Enqueue(Node, T, EUIEventType::Clicked); }
	return Hit;
}

/** 标签文本布局：图标在前（有图标时）—— 返回文本可用区。 */
inline FUIRect LabelRect(const FUIBuilder& Node, bool bHasIcon)
{
	const FUIRect Content = ContentRectOf(Node);
	if (!bHasIcon) { return Content; }
	const float IconSpan = Node.GetResolvedStyle().IconSize + 6.f;
	return FUIRect{ Content.X + IconSpan, Content.Y, Content.W - IconSpan, Content.H };
}

/** 行内图标区（左端方框）。 */
inline FUIRect IconRect(const FUIBuilder& Node)
{
	const FUIRect Content = ContentRectOf(Node);
	const float Size = Node.GetResolvedStyle().IconSize;
	return FUIRect{ Content.X, Content.Y + (Content.H - Size) * 0.5f, Size, Size };
}

}}} // namespace Maho::UI::Detail
