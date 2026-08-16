#include <Paths.h>

namespace Maho::Paths
{

bool FPaths::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = resolve project/engine roots; Shutdown = nothing.
	(void)Stage;
	return true;
}

} // namespace Maho::Paths

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FPathsAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Paths::FPaths::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_PATHS_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FPathsAdapter();
}
