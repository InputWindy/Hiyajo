#pragma once

#include "TimerApi.h"
#include <Engine.h>

#include <chrono>

namespace Maho
{

namespace Timer
{

/** Time source extension (chrono). Pre-app toolkit (driven by EToolStage). */
class MAHO_TIMER_API FTimer final : public TExtension<EToolStage, FTimer>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

	/** Seconds since Init (wall clock). */
	[[nodiscard]] double GetRealSeconds() const;

	/** Game seconds (scaled by TimeScale, frozen while paused). Lazily advanced. */
	[[nodiscard]] double GetGameSeconds();

	/** Game-time delta since the previous Advance, in seconds. */
	[[nodiscard]] double GetDeltaSeconds();

	void SetTimeScale(double InScale);
	void SetPaused(bool bPaused);
	[[nodiscard]] double GetTimeScale() const;
	[[nodiscard]] bool IsPaused() const;

private:
	friend TSingleton<FTimer>;
	FTimer() = default;

	double GameSeconds = 0.0;
	double DeltaSeconds = 0.0;
	double TimeScale = 1.0;
	bool bPaused = false;
	std::chrono::steady_clock::time_point InitTime{};
	std::chrono::steady_clock::time_point LastReal{};
};

} // namespace Timer

} // namespace Maho
