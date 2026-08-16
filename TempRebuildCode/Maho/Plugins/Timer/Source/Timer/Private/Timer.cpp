#include <Timer.h>

#include <chrono>

namespace Maho::Timer
{

bool FTimer::ExecuteStage(EToolStage Stage)
{
	switch (Stage)
	{
	case EToolStage::Init:
		InitTime = std::chrono::steady_clock::now();
		LastReal = InitTime;
		break;

	case EToolStage::Shutdown:
		break;
	}

	return true;
}

double FTimer::GetRealSeconds() const
{
	const auto Now = std::chrono::steady_clock::now();
	const std::chrono::duration<double> Elapsed = Now - InitTime;
	return Elapsed.count();
}

double FTimer::GetGameSeconds()
{
	const auto Now = std::chrono::steady_clock::now();

	if (!bPaused)
	{
		const std::chrono::duration<double> RealDelta = Now - LastReal;
		GameSeconds += RealDelta.count() * TimeScale;
		DeltaSeconds = RealDelta.count() * TimeScale;
	}

	LastReal = Now;
	return GameSeconds;
}

double FTimer::GetDeltaSeconds()
{
	GetGameSeconds();
	return DeltaSeconds;
}

void FTimer::SetTimeScale(double InScale)
{
	TimeScale = InScale;
}

void FTimer::SetPaused(bool bInPaused)
{
	if (bInPaused && !bPaused)
	{
		GetGameSeconds();
	}

	bPaused = bInPaused;
}

double FTimer::GetTimeScale() const
{
	return TimeScale;
}

bool FTimer::IsPaused() const
{
	return bPaused;
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
