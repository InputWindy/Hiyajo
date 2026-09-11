#pragma once

#include <FUIBuilder.h>

namespace Maho { namespace UI {

/** 两趟布局：Measure 自底向上量 Content 尺寸，Arrange 自顶向下分配矩形。
 *  算法只在这里实现一次；容器若需自定义排布，覆盖 `FUIBuilder::ArrangeChildren`。 */
struct FUILayoutEngine
{
	/** Content 尺寸（不含本节点 Margin；含本节点 Padding）。 */
	static FUIVector2 Measure(FUIBuilder& Node, IUITranslator& T, const FUIVector2& Available);

	/** 按 `Node` 的 Layout 摆放其可见子节点并逐个 `Translate`（默认 ArrangeChildren）。 */
	static void ArrangeChildren(FUIBuilder& Node, IUITranslator& T);

	/** 同上，但内容区显式给出（Grid 网格 / Tree 缩进等自定义排布复用同一算法）。 */
	static void ArrangeIn(FUIBuilder& Node, IUITranslator& T, const FUIRect& Content);

	/** 本节点外框（已扣 Margin）。 */
	static FUIRect FrameRect(const FUIBuilder& Node, const FUIRect& Allocated);

	/** 外框再扣内边距 = 子节点可用区。
	 *  内边距来源：`FUILayout::Padding` 非零则用它，否则用解析样式的 `Padding`。 */
	static FUIRect ContentRect(const FUIBuilder& Node, const FUIRect& Frame);

	/** `ContentRect` 的逆：子树尺寸 + 本节点自身内边距 = 外框（左上角在本节点原点）。
	 *  `FUIBuilder::MeasureContent` 给的是**子树**尺寸，本节点内边距不在其中；要先经这里补回
	 *  再交给 `ContentRect`，否则同一段内边距被扣两次（浮层调用方算内容起点就少这一段）。 */
	static FUIRect OuterRect(const FUIBuilder& Node, const FUIVector2& Content);
};

}} // namespace Maho::UI
