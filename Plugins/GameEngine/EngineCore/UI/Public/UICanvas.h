#pragma once

#include "FUIBuilder.h"

namespace Maho { namespace UI {

/** 根画布：视图的树根（也是 HUD/自由布局的载体）。
 *  无外观、无裁剪 —— 只按 `Layout()` 排布子节点；根矩形由宿主给（内容区或视口）。 */
class MAHO_UI_API FUICanvas final : public FUIBuilder
{
public:
	explicit FUICanvas(FUIName InId);
	~FUICanvas() override;

protected:
	[[nodiscard]] std::string_view TypeName() const override;
	[[nodiscard]] const FUIStyle& TypeDefaultStyle() const override;
};

}} // namespace Maho::UI
