#pragma once

#include "ConsoleVariableApi.h"
#include <Engine.h>

namespace Maho
{

/** Console variable registry extension. Pre-app singleton (driven by ESingletonStage). */
class MAHO_CONSOLEVARIABLE_API FConsoleVariable final : public TExtension<ESingletonStage, FConsoleVariable>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FConsoleVariable>;
	FConsoleVariable() = default;
};

} // namespace Maho
