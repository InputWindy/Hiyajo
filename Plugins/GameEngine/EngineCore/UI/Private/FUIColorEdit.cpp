// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUIColorEdit.h>

#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

namespace
{
/** 色块最小可视宽度：标签之外的编辑空间（布局给不下时按可用宽压缩）。 */
constexpr float kMinColorWidth = 120.f;
} // namespace

FUIColorEdit::FUIColorEdit(FUIName InId)
	: FUIBuilder(InId)
{
}

FUIColorEdit::~FUIColorEdit() = default;

std::string_view FUIColorEdit::TypeName() const
{
	return "FUIColorEdit";
}

FUIColorEdit& FUIColorEdit::SetValue(const FUIColor& InColor)
{
	RGBA[0] = InColor.R;
	RGBA[1] = InColor.G;
	RGBA[2] = InColor.B;
	RGBA[3] = InColor.A;
	bValueDirty = true;
	return *this;
}

FUIColor FUIColorEdit::GetValue() const
{
	return FUIColor{ RGBA[0], RGBA[1], RGBA[2], RGBA[3] };
}

const FUIStyle& FUIColorEdit::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		// 字段色（`Field*`）：色块那支由后端自己画，内里吃这一套（与 `Control*` 那支中灰分开）。
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

FUIVector2 FUIColorEdit::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	const FUIResolvedStyle& S = GetResolvedStyle();
	float LabelW = LabelWidth;
	if (LabelW <= 0.f && !Label.empty())
	{
		const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
		LabelW = T.MeasureText(Label, Font, S.FontSize).X + 8.f;
	}
	const float EditW = std::min(kMinColorWidth, std::max(0.f, Available.X - LabelW));
	// 框高与后端同源：ImGui 的框高 = 字号 + `FramePadding.y`×2（翻译器把声明的 `Padding` 压成
	// `FramePadding`，见 `PushControlStyle`），量不一样高就会压住下面的兄弟。
	return FUIVector2{ LabelW + EditW, S.FontSize + S.Padding.Top + S.Padding.Bottom };
}

void FUIColorEdit::PaintSelf(IUITranslator& T, const FUIResolvedStyle& S)
{
	const FUIRect Content = Detail::ContentRectOf(*this);
	float LabelW = LabelWidth;
	if (LabelW <= 0.f && !Label.empty())
	{
		const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
		LabelW = T.MeasureText(Label, Font, S.FontSize).X + 8.f;
	}
	const FUIRect Track{ Content.X + LabelW, Content.Y, std::max(1.f, Content.W - LabelW), Content.H };

	const FUIHitResult Hit = T.WidgetColorEdit(GetId(), Track, RGBA, S);
	Detail::WriteHit(*this, Hit);
}

void FUIColorEdit::PaintContent(IUITranslator& T, const FUIResolvedStyle& S)
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

EUIInputFlags FUIColorEdit::GetInputFlags() const
{
	return FUIBuilder::GetInputFlags() | EUIInputFlags::HitTest;
}

void FUIColorEdit::SyncConfig(const FUIBuilder& Declared)
{
	const FUIColorEdit* Other = dynamic_cast<const FUIColorEdit*>(&Declared);
	if (Other == nullptr) { return; }
	Label = Other->Label;
	LabelWidth = Other->LabelWidth;
	// 值：只在声明侧显式给过时采纳，否则保留后端写入的运行期值（编辑不被陈旧声明压回）
	if (Other->bValueDirty)
	{
		for (int i = 0; i < 4; ++i) { RGBA[i] = Other->RGBA[i]; }
	}
}

}} // namespace Maho::UI
