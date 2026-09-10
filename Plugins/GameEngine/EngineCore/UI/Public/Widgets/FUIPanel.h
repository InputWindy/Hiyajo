#pragma once

#include "FUIBuilder.h"

#include <string>

namespace Maho { namespace UI {

/** 面板自绘范围：`None` = 只当容器（真窗口外壳已画标题/边框）；
 *  `Full` = 自绘面板外观（无窗口叠加层 / 子区域）。 */
enum class EUIPanelChrome : std::uint8_t
{
	None,
	Full
};

/** 面板：可选锚点比例、可选自绘外观、可选滚动区。 */
class MAHO_UI_API FUIPanel final : public FUIBuilder
{
public:
	explicit FUIPanel(FUIName InId);
	~FUIPanel() override;

	/** 显示区比例（0..1）：覆盖父布局分配，直接按视图显示区取矩形（旧 `FUIWidget` 语义）。 */
	FUIPanel& SetAnchor(float InX, float InY, float InW, float InH);
	FUIPanel& SetTitle(std::string_view InTitle) { Title.assign(InTitle); return *this; }
	FUIPanel& SetClosable(bool bIn) { bClosable = bIn; return *this; }
	FUIPanel& SetChrome(EUIPanelChrome C) { Chrome = C; return *this; }
	FUIPanel& SetScrollable(bool bIn) { bScrollable = bIn; return *this; }
	FUIPanel& SetHeaderHeight(float H) { HeaderHeight = H; return *this; }

	[[nodiscard]] bool HasAnchor() const { return bHasAnchor; }
	[[nodiscard]] const std::string& GetTitle() const { return Title; }

protected:
	std::string_view TypeName() const override;
	FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override;
	[[nodiscard]] FUIRect ResolveFrame(const FUIRect& Allocated, IUITranslator& T) const override;
	void       PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override;
	void       ArrangeChildren(IUITranslator& T) override;
	EUIInputFlags GetInputFlags() const override;
	const FUIStyle& TypeDefaultStyle() const override;
	void       SyncConfig(const FUIBuilder& Declared) override;

private:
	[[nodiscard]] float  UsedHeaderHeight() const;
	[[nodiscard]] FUIRect BodyRect() const;

	std::string    Title;
	FUIName        CloseId{};      // 关闭框的内部命中 Id（由面板 Id 派生）
	EUIPanelChrome Chrome = EUIPanelChrome::None;
	bool  bClosable = false;
	bool  bScrollable = false;
	bool  bHasAnchor = false;
	float AnchorX = 0.f, AnchorY = 0.f, AnchorW = 1.f, AnchorH = 1.f;
	float HeaderHeight = 24.f;
};

}} // namespace Maho::UI
