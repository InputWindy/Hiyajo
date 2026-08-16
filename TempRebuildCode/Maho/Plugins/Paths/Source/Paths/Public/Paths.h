#pragma once

#include "PathsApi.h"
#include <Engine.h>

namespace Maho
{

namespace Paths
{

/** Path resolution extension (project/engine roots). Pre-app singleton (driven by ESingletonStage). */
class MAHO_PATHS_API FPaths final : public TExtension<ESingletonStage, FPaths>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FPaths>;
	FPaths() = default;
};

} // namespace Paths

} // namespace Maho
