#pragma once

#include "ConsoleVariableApi.h"
#include <Engine.h>

namespace Maho
{

namespace ConsoleVariable
{

/** Console variable registry extension. Pre-app toolkit (driven by EToolStage). */
class MAHO_CONSOLEVARIABLE_API FConsoleVariable final : public TExtension<EToolStage, FConsoleVariable>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FConsoleVariable>;
	FConsoleVariable() = default;
};

} // namespace ConsoleVariable

} // namespace Maho
