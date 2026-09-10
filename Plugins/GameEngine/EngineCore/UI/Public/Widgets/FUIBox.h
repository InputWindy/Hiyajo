#pragma once

#include "FUIBuilder.h"

namespace Maho { namespace UI {

/** 纯容器：分组 / 行 / 列。外观透明，排布走 `Layout()`。 */
class MAHO_UI_API FUIBox final : public FUIBuilder
{
public:
	explicit FUIBox(FUIName InId);
	~FUIBox() override;

	// 布局糖（等价 `Layout().SetXxx`），链式返回本节点
	FUIBox& SetDirection(EUIDirection D) { Layout().SetDirection(D); return *this; }
	FUIBox& SetSpacing(float S) { Layout().SetSpacing(S); return *this; }
	FUIBox& SetPadding(FMargin P) { Layout().SetPadding(P); return *this; }
	FUIBox& SetSize(FUILength W, FUILength H) { Layout().SetSize(W, H); return *this; }
	FUIBox& SetAlign(EUIAlign H, EUIAlign V) { Layout().HorizontalAlign = H; Layout().VerticalAlign = V; return *this; }
	FUIBox& SetMargin(FMargin M) { Layout().Margin = M; return *this; }

protected:
	std::string_view TypeName() const override;
	const FUIStyle& TypeDefaultStyle() const override;
};

}} // namespace Maho::UI
