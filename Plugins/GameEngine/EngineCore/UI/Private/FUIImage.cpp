// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUIImage.h>

#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

FUIImage::FUIImage(FUIName InId)
	: FUIBuilder(InId)
{
}

FUIImage::~FUIImage() = default;

std::string_view FUIImage::TypeName() const
{
	return "FUIImage";
}

const FUIStyle& FUIImage::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		// 图片不画底/边：只借用前景色的 Alpha 通道作为整体不透明度
		S[EUIState::Normal].Text     = Th.Text;
		S[EUIState::Normal].FontSize = Th.FontSize;
		S[EUIState::Hovered].Text    = Th.Text;
		S[EUIState::Pressed].Text    = Th.Text;
		S[EUIState::Selected].Text   = Th.Text;
		S[EUIState::Disabled].Text   = Th.TextDisabled;
	});
}

FUIVector2 FUIImage::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	(void)Available;
	if (Texture.IsNone()) { return FUIVector2{ 0.f, 0.f }; }

	// 资源未就绪按零尺寸（不占位、不阻塞布局；就绪后自动接续）
	const FUIResolvedResource Res = T.ResolveTexture(Texture);
	if (!Res.bValid || Res.Width == 0 || Res.Height == 0) { return FUIVector2{ 0.f, 0.f }; }
	if (ScaleMode == EUIScaleMode::Stretch) { return FUIVector2{ 0.f, 0.f }; }   // 拉伸：尺寸由布局给

	return FUIVector2{ static_cast<float>(Res.Width), static_cast<float>(Res.Height) };
}

void FUIImage::PaintContent(IUITranslator& T, const FUIResolvedStyle& S)
{
	if (Texture.IsNone()) { return; }

	const FUIResolvedResource Res = T.ResolveTexture(Texture);
	const FUIRect Content = Detail::ContentRectOf(*this);
	if (Content.IsEmpty()) { return; }

	// `SetTint` 的着色纹理 v1 不参与绘制（`DrawImage` 只吃颜色通道）：字段保留给后续后端
	FUIColor Tint = TintColor;
	Tint.A *= S.Text.A;   // 禁用态由前景色 Alpha 统一压暗

	FUIRect Dst = Content;
	if (ScaleMode != EUIScaleMode::Stretch && Res.bValid && Res.Width > 0 && Res.Height > 0)
	{
		const float TexAspect = static_cast<float>(Res.Width) / static_cast<float>(Res.Height);
		const float BoxAspect = Content.W / Content.H;
		// Fit：框比图"宽"时以高为准；Fill：反之
		const bool bFit = ScaleMode == EUIScaleMode::Fit;
		if ((BoxAspect > TexAspect) == bFit)
		{
			Dst.W = Content.H * TexAspect;
		}
		else
		{
			Dst.H = Content.W / TexAspect;
		}
		Dst.X = Content.X + (Content.W - Dst.W) * 0.5f;
		Dst.Y = Content.Y + (Content.H - Dst.H) * 0.5f;
	}

	T.DrawImage(Dst, Res, Tint, UV0, UV1);
}

void FUIImage::SyncConfig(const FUIBuilder& Declared)
{
	const FUIImage* Other = dynamic_cast<const FUIImage*>(&Declared);
	if (Other == nullptr) { return; }
	Texture = Other->Texture;
	Tint = Other->Tint;
	TintColor = Other->TintColor;
	UV0 = Other->UV0;
	UV1 = Other->UV1;
	ScaleMode = Other->ScaleMode;
}

}} // namespace Maho::UI
