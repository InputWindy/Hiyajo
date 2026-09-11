// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUIInputText.h>

#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

FUIInputText::FUIInputText(FUIName InId)
	: FUIBuilder(InId)
{
}

FUIInputText::~FUIInputText() = default;

std::string_view FUIInputText::TypeName() const
{
	return "FUIInputText";
}

const FUIStyle& FUIInputText::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		// 字段色（`Field*`）：这支近黑与 `Control*` 那支中灰分开，后端画的输入框内里吃的是这一套。
		S[EUIState::Normal].Fill      = Th.FieldFill;
		S[EUIState::Normal].Stroke    = Th.FieldStroke;
		S[EUIState::Normal].Text      = Th.Text;
		S[EUIState::Normal].FontSize  = Th.FontSize;
		S[EUIState::Normal].Radius    = Th.Radius;
		S[EUIState::Normal].StrokeWidth = Th.StrokeWidth;
		S[EUIState::Normal].Padding   = FMargin(6.f, 4.f);

		S[EUIState::Hovered].Fill   = Th.FieldHover;
		S[EUIState::Hovered].Stroke = Th.FieldStroke;
		S[EUIState::Hovered].Text   = Th.Text;

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

FUIVector2 FUIInputText::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	const FUIResolvedStyle& S = GetResolvedStyle();
	const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
	const std::string_view Shown = Value.empty() ? std::string_view(Hint) : std::string_view(Value);
	const FUIVector2 TextSize = T.MeasureText(Shown, Font, S.FontSize);

	// 多行：按可用高（缺省 3 行）撑开；单行：**与后端同源的框高**。ImGui 的单行框高 =
	// 字号 + `FramePadding.y`×2（`imgui_widgets.cpp` 的 `InputText`：`label_size.y + style.FramePadding.y*2.f`），
	// 而翻译器把声明的 `Padding` 压成了 `FramePadding`（`PushControlStyle`）：测量与压栈不同源，
	// 声明侧量出来的高就与屏上那个框对不上（框比布局给的矩形高，压住下面的兄弟）。
	const float LineH = S.FontSize * 1.4f;
	const float H = bMultiline ? std::max(LineH * 3.f, Available.Y)
							   : S.FontSize + S.Padding.Top + S.Padding.Bottom;
	return FUIVector2{ TextSize.X + S.FontSize * 0.6f, H };
}

void FUIInputText::PaintSelf(IUITranslator& T, const FUIResolvedStyle& S)
{
	const std::string Before = Value;
	const FUIHitResult Hit = T.WidgetInputText(GetId(), GetRect(), Value, Hint, MaxLength, bMultiline, S);
	Detail::WriteHit(*this, Hit);

	// 后端只报告"本帧文本变了/回车了"：事件由这里入队（回调归所有者线程）
	if (Hit.bClicked && Value != Before)
	{
		Detail::Enqueue(*this, T, EUIEventType::TextChanged, 0.f, false, Value);
	}
	if (Hit.bSubmitted)
	{
		Detail::Enqueue(*this, T, EUIEventType::Submitted, 0.f, false, Value);
	}
}

EUIInputFlags FUIInputText::GetInputFlags() const
{
	return FUIBuilder::GetInputFlags() | EUIInputFlags::HitTest;
}

void FUIInputText::SyncConfig(const FUIBuilder& Declared)
{
	const FUIInputText* Other = dynamic_cast<const FUIInputText*>(&Declared);
	if (Other == nullptr) { return; }
	Value = Other->bValueDirty ? Other->Value : Value;   // 只认显式声明；否则保留后端写入的输入值
	Hint = Other->Hint;
	MaxLength = Other->MaxLength;
	bMultiline = Other->bMultiline;
}

}} // namespace Maho::UI
