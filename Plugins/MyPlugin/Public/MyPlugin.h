#pragma once

#include "MyPluginApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

namespace Maho
{

// MyPlugin — a Layer node: FLayer<children...> + the interfaces it
// implements. Hand-write the children (Layers) and interfaces (IPlugin<...>)
// you want; the root (FLayerBase), scheduler, install/uninstall lifecycle
// and the LINQ query all come from FLayer.
class FMyPlugin
	: public FLayer<>
	, public IPlugin<
#ifdef MAHO_MYPLUGIN_INTERFACES
		MAHO_MYPLUGIN_INTERFACES
#endif
	>
{
public:
	// The module-instance factory — the only way an FLayerBase root is born.
	static FLayerBase* CreateExtension();

	// The module path this layer's DLL is loaded from.
	static std::string_view GetModulePath() { return "MyPlugin.dll"; }

	// Install what this layer manages (recursive). Your loop lives in Main().
	void OnInstall() override {}
};

} // namespace Maho
