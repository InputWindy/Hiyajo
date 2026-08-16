#pragma once

#include "TimerApi.h"
#include <Engine.h>

namespace Maho
{

namespace Timer
{

/** Time source extension (chrono). Pre-app toolkit (driven by EToolStage). */
class MAHO_TIMER_API FTimer final : public TExtension<EToolStage, FTimer>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FTimer>;
	FTimer() = default;
};

} // namespace Timer

} // namespace Maho
