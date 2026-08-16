#include <Archive.h>

namespace Maho::Archive
{

bool FArchive::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = nothing; Shutdown = nothing.
	(void)Stage;
	return true;
}

} // namespace Maho::Archive

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FArchiveAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Archive::FArchive::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_ARCHIVE_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FArchiveAdapter();
}
