#include <UICanvas.h>

namespace Maho { namespace UI {

FUICanvas::FUICanvas(FUIName InId)
	: FUIBuilder(InId)
{
}

FUICanvas::~FUICanvas() = default;

std::string_view FUICanvas::TypeName() const
{
	return "FUICanvas";
}

const FUIStyle& FUICanvas::TypeDefaultStyle() const
{
	static const FUIStyle CanvasStyle;
	return CanvasStyle;
}

}} // namespace Maho::UI
