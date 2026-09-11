// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUISliderFloat.h>

#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

namespace
{
/** 滑条最小可视宽度：标签之外的滑轨空间（布局给不下时按可用宽压缩）。 */
constexpr float kMinSliderWidth = 80.f;
} // namespace

FUISliderFloat::FUISliderFloat(FUIName InId)
	: FUIBuilder(InId)
{
}

FUISliderFloat::~FUISliderFloat() = default;

std::string_view FUISliderFloat::TypeName() const
{
	return "FUISliderFloat";
}

const FUIStyle& FUISliderFloat::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		// 字段色（`Field*`）：滑条后端自己画框，内里吃这一套（与 `Control*` 那支中灰分开）。
		S[EUIState::Normal].Fill      = Th.FieldFill;
		S[EUIState::Normal].Stroke    = Th.FieldStroke;
		S[EUIState::Normal].Text      = Th.Text;
		S[EUIState::Normal].FontSize  = Th.FontSize;
		S[EUIState::Normal].Radius    = Th.Radius;
		S[EUIState::Normal].StrokeWidth = Th.StrokeWidth;
		S[EUIState::Normal].Padding   = FMargin(4.f, 2.f);

		S[EUIState::Hovered].Fill = Th.FieldHover;
		S[EUIState::Hovered].Text = Th.Text;

		S[EUIState::Pressed].Fill   = Th.FieldFill;
		S[EUIState::Pressed].Stroke = Th.Accent;
		S[EUIState::Pressed].Text   = Th.Text;

		S[EUIState::Selected].Fill   = Th.FieldFill;
		S[EUIState::Selected].Stroke = Th.Accent;
		S[EUIState::Selected].Text   = Th.Text;

		S[EUIState::Disabled].Fill = Th.FieldFill;
		S[EUIState::Disabled].Text = Th.TextDisabled;
	});
}

FUIVector2 FUISliderFloat::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	const FUIResolvedStyle& S = GetResolvedStyle();
	float LabelW = LabelWidth;
	if (LabelW <= 0.f && !Label.empty())
	{
		const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
		LabelW = T.MeasureText(Label, Font, S.FontSize).X + 8.f;
	}
	const float SliderW = std::min(kMinSliderWidth, std::max(0.f, Available.X - LabelW));
	// 框高与后端同源：ImGui 的框高 = 字号 + `FramePadding.y`×2（翻译器把声明的 `Padding` 压成
	// `FramePadding`，见 `PushControlStyle`），量不一样高就会压住下面的兄弟。
	return FUIVector2{ LabelW + SliderW, S.FontSize + S.Padding.Top + S.Padding.Bottom };
}

void FUISliderFloat::PaintSelf(IUITranslator& T, const FUIResolvedStyle& S)
{
	const FUIRect Content = Detail::ContentRectOf(*this);
	float LabelW = LabelWidth;
	if (LabelW <= 0.f && !Label.empty())
	{
		const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
		LabelW = T.MeasureText(Label, Font, S.FontSize).X + 8.f;
	}
	const FUIRect Track{ Content.X + LabelW, Content.Y, std::max(1.f, Content.W - LabelW), Content.H };

	const FUIHitResult Hit = T.WidgetSliderFloat(GetId(), Track, Value, Min, Max, Format, S);
	Detail::WriteHit(*this, Hit);
	// 后端只报"本帧值变了"：变更事件由这里入队（回调归所有者线程）
	if (Hit.bClicked && !IsDisabled())
	{
		Detail::Enqueue(*this, T, EUIEventType::ValueChanged, Value);
	}
}

void FUISliderFloat::PaintContent(IUITranslator& T, const FUIResolvedStyle& S)
{
	if (Label.empty()) { return; }
	const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
	const FUIRect Content = Detail::ContentRectOf(*this);
	float LabelW = LabelWidth;
	if (LabelW <= 0.f)
	{
		LabelW = T.MeasureText(Label, Font, S.FontSize).X + 8.f;
	}
	T.DrawText(FUIRect{ Content.X, Content.Y, LabelW, Content.H }, Label, Font, S.FontSize, S, EUITextAlign::Left);
}

EUIInputFlags FUISliderFloat::GetInputFlags() const
{
	return FUIBuilder::GetInputFlags() | EUIInputFlags::HitTest;
}

void FUISliderFloat::SyncConfig(const FUIBuilder& Declared)
{
	const FUISliderFloat* Other = dynamic_cast<const FUISliderFloat*>(&Declared);
	if (Other == nullptr) { return; }
	Label = Other->Label;
	Format = Other->Format;
	LabelWidth = Other->LabelWidth;
	Min = Other->Min;
	Max = Other->Max;
	// 值：只在声明侧显式给过时采纳，否则保留后端写入的运行期值（拖动不被陈旧声明压回）
	if (Other->bValueDirty) { Value = Other->Value; }
}

}} // namespace Maho::UI
