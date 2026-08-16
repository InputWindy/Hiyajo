#include <Timer.h>

namespace Maho::Timer
{

bool FTimer::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = start steady clock; Shutdown = nothing.
	(void)Stage;
	return true;
}

} // namespace Maho::Timer

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FTimerAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Timer::FTimer::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_TIMER_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FTimerAdapter();
}
