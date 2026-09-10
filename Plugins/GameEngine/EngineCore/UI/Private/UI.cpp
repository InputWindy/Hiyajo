// UI 插件的动态装载入口。本模块只有一个层：FUIViewRegistry（见 UIViewRegistry.cpp）。
#include <UIViewRegistry.h>

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_UI_API Maho::FLayerBase* CreateLayer()
{
	return Maho::UI::FUIViewRegistry::CreateLayer();
}
