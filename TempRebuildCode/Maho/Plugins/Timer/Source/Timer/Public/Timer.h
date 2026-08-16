#pragma once

#include "TimerApi.h"
#include <Engine.h>

namespace Maho
{

namespace Timer
{

/** Time source extension (chrono). Pre-app singleton (driven by ESingletonStage). */
class MAHO_TIMER_API FTimer final : public TExtension<ESingletonStage, FTimer>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FTimer>;
	FTimer() = default;
};

} // namespace Timer

} // namespace Maho
