// Compile check for MAHO_DECLARE_LAYER + the plugin scaffold — the layer stays
// CONCRETE because every FLayer-uncovered abstract capability is overridden.
#include <Engine/Layer.h>

using namespace Maho;

// a plugin layer exactly as create_plugin scaffolds it
class FMyPlugin
	: public FLayer<>
	, public IPlugin<IInit, IShutdown, IMain, IExit>
{
	MAHO_DECLARE_LAYER(FMyPlugin, "MyPlugin.dll");

	int Main() override { return 0; } // the run entry (a layer that owns a loop)
	void Exit() override {}
	void Initialize(int, char**) override {} // optional lifecycle hook
	void Shutdown() override {}
};

int main()
{
	FMyPlugin P;
	if (P.GetModulePath() != "MyPlugin.dll")
	{
		return 1;
	}
	// the factory returns the anonymous FLayerBase* ceiling — downcast to drive
	auto* Made = FMyPlugin::CreateLayer();
	if (Made == nullptr)
	{
		return 2;
	}
	FMyPlugin* Typed = static_cast<FMyPlugin*>(Made);
	Typed->Initialize(0, nullptr);
	Typed->Shutdown();
	Typed->Main();
	Typed->Exit();
	delete Made;
	return 0;
}
