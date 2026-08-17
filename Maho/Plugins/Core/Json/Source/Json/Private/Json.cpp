#include <Json.h>

namespace Maho::Json
{

bool FJson::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = nothing (header-only); Shutdown = nothing.
	(void)Stage;
	return true;
}

} // namespace Maho::Json

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FJsonAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Json::FJson::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_JSON_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FJsonAdapter();
}
