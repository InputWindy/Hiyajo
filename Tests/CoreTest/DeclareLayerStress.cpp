// Compile check for MAHO_DECLARE_LAYER — the plugin scaffold macro.
#include <Engine/Layer.h>

using namespace Maho;

// a plugin layer declared via the macro (hand-written IPlugin<> interfaces)
class FMyPlugin
	: public FLayer<>
	, public IPlugin<IMain>
{
	MAHO_DECLARE_LAYER(FMyPlugin, "MyPlugin.dll");
	int Main() override { return 0; }
};

int main()
{
	FMyPlugin P;
	if (P.GetModulePath() != "MyPlugin.dll")
	{
		return 1;
	}
	if (FMyPlugin::CreateExtension() == nullptr)
	{
		return 2;
	}
	delete FMyPlugin::CreateExtension();
	P.OnInstall();
	return 0;
}
