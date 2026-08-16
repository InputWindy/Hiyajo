#pragma once

#include "ConfigApi.h"
#include <Engine.h>

namespace Maho
{

namespace Config
{

/** Configuration file extension (JSON). Pre-app toolkit (driven by EToolStage). */
class MAHO_CONFIG_API FConfig final : public TExtension<EToolStage, FConfig>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FConfig>;
	FConfig() = default;
};

} // namespace Config

} // namespace Maho
