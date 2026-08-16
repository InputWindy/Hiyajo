#pragma once

#include "PathsApi.h"
#include <Engine.h>

namespace Maho
{

namespace Paths
{

/** Path resolution extension (project/engine roots). Pre-app toolkit (driven by EToolStage). */
class MAHO_PATHS_API FPaths final : public TExtension<EToolStage, FPaths>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FPaths>;
	FPaths() = default;
};

} // namespace Paths

} // namespace Maho
