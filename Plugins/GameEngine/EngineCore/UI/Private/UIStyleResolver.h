#pragma once

#include <FUIBuilder.h>

namespace Maho { namespace UI {

/** 样式回退链：实例覆盖 → 父节点已解析值 → 类型默认 → （主题 token，§4.7 接入）。
 *  逐"项"回退（每个 optional 独立走链）。
 *  **可继承的只有前景类项**（Text/Font/FontSize/Icon/IconSize）：它们是"逐级传导"的语义；
 *  背景/描边/圆角/内边距属"容器自绘"，继承会让子容器套上父容器底色，故不继承。 */
struct FUIStyleResolver
{
	/** 解析本节点本帧生效样式。`ParentResolved` 为 nullptr 表示根节点。 */
	static FUIResolvedStyle Resolve(const FUIBuilder& Node, const FUIResolvedStyle* ParentResolved);
};

}} // namespace Maho::UI
