#include <TestPlugin.h>

namespace Maho
{

namespace TestPlugin
{

bool FTestPlugin::ExecuteStage(EToolStage Stage)
{
	// TODO: per-stage behavior.
	(void)Stage;
	return true;
}

} // namespace TestPlugin

} // namespace Maho


// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FTestPluginAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::TestPlugin::FTestPlugin::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_TESTPLUGIN_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FTestPluginAdapter();
}
