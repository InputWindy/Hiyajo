#pragma once

#include "LogApi.h"
#include <Engine.h>

namespace Maho
{

namespace Log
{

/** Logging extension (spdlog). Pre-app toolkit (driven by EToolStage). */
class MAHO_LOG_API FLogger final : public TExtension<EToolStage, FLogger>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FLogger>;
	FLogger() = default;
};

} // namespace Log

} // namespace Maho
