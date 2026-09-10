#pragma once

#include "FUIBuilder.h"

namespace Maho { namespace UI {

/** 图片：资源引用 `FUIName`，由翻译层解析成原生句柄后绘制。 */
class MAHO_UI_API FUIImage final : public FUIBuilder
{
public:
	explicit FUIImage(FUIName InId);
	~FUIImage() override;

	FUIImage& SetTexture(FUIName InTexture) { Texture = InTexture; return *this; }
	FUIImage& SetUV(float U0, float V0, float U1, float V1) { UV0 = { U0, V0 }; UV1 = { U1, V1 }; return *this; }
	FUIImage& SetTint(FUIName InTint) { Tint = InTint; return *this; }
	FUIImage& SetTintColor(FUIColor InColor) { TintColor = InColor; return *this; }
	FUIImage& SetScaleMode(EUIScaleMode InMode) { ScaleMode = InMode; return *this; }

	[[nodiscard]] FUIName GetTexture() const { return Texture; }

protected:
	std::string_view TypeName() const override;
	FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override;
	void       PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override;
	const FUIStyle& TypeDefaultStyle() const override;
	void       SyncConfig(const FUIBuilder& Declared) override;

private:
	FUIName         Texture{};
	FUIName         Tint{};
	FUIColor        TintColor{ 1.f, 1.f, 1.f, 1.f };
	FUIVector2      UV0{ 0.f, 0.f };
	FUIVector2      UV1{ 1.f, 1.f };
	EUIScaleMode    ScaleMode = EUIScaleMode::Fit;
};

}} // namespace Maho::UI
