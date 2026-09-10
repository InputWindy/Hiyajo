// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUIBox.h>

#include "WidgetStyle.h"

namespace Maho { namespace UI {

FUIBox::FUIBox(FUIName InId)
	: FUIBuilder(InId)
{
}

FUIBox::~FUIBox() = default;

std::string_view FUIBox::TypeName() const
{
	return "FUIBox";
}

const FUIStyle& FUIBox::TypeDefaultStyle() const
{
	// 纯容器：不画底/边，只提供前景（Text/FontSize）供子节点继承 —— 故五态都有取值。
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		S[EUIState::Normal].Text     = Th.Text;
		S[EUIState::Normal].FontSize = Th.FontSize;

		S[EUIState::Hovered].Text = Th.Text;
		S[EUIState::Pressed].Text = Th.Text;
		S[EUIState::Selected].Text = Th.Text;
		S[EUIState::Disabled].Text = Th.TextDisabled;
	});
}

}} // namespace Maho::UI
