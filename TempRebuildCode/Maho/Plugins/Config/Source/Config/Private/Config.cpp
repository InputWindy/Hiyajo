#include <Config.h>

namespace Maho::Config
{

bool FConfig::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = load config; Shutdown = save if dirty.
	(void)Stage;
	return true;
}

} // namespace Maho::Config

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FConfigAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Config::FConfig::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_CONFIG_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FConfigAdapter();
}
