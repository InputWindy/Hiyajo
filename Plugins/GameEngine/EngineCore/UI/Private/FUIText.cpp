// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUIText.h>

#include "WidgetStyle.h"

namespace Maho { namespace UI {

FUIText::FUIText(FUIName InId)
	: FUIBuilder(InId)
{
}

FUIText::~FUIText() = default;

std::string_view FUIText::TypeName() const
{
	return "FUIText";
}

const FUIStyle& FUIText::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		S[EUIState::Normal].Text     = Th.Text;
		S[EUIState::Normal].FontSize = Th.FontSize;

		S[EUIState::Hovered].Text = Th.Text;
		S[EUIState::Pressed].Text = Th.Text;
		S[EUIState::Selected].Text = Th.Accent;
		S[EUIState::Disabled].Text = Th.TextDisabled;
	});
}

FUIVector2 FUIText::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	(void)Available;   // v1：`SetWrap` 只影响绘制时的裁剪，不参与测量
	const FUIResolvedStyle& S = GetResolvedStyle();
	const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
	return T.MeasureText(Text, Font, S.FontSize);
}

void FUIText::PaintContent(IUITranslator& T, const FUIResolvedStyle& S)
{
	const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
	T.DrawText(GetRect(), Text, Font, S.FontSize, S, Align);
}

void FUIText::SyncConfig(const FUIBuilder& Declared)
{
	const FUIText* Other = dynamic_cast<const FUIText*>(&Declared);
	if (Other == nullptr) { return; }
	Text = Other->Text;
	Align = Other->Align;
	bWrap = Other->bWrap;
}

}} // namespace Maho::UI
