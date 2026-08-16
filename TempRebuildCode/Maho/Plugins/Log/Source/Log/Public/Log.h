#pragma once

#include "LogApi.h"
#include <Engine.h>

namespace Maho
{

namespace Log
{

/** Logging extension (spdlog). Pre-app singleton (driven by ESingletonStage). */
class MAHO_LOG_API FLogger final : public TExtension<ESingletonStage, FLogger>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FLogger>;
	FLogger() = default;
};

} // namespace Log

} // namespace Maho
