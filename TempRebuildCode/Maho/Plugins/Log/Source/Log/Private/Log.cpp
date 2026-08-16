#include <Log.h>

namespace Maho::Log
{

bool FLogger::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = init spdlog sinks; Shutdown = flush + drop.
	(void)Stage;
	return true;
}

} // namespace Maho::Log

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FLoggerAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Log::FLogger::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_LOG_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FLoggerAdapter();
}
