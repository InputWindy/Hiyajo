#pragma once

#include "ConfigApi.h"
#include <Engine.h>

namespace Maho
{

/** Configuration file extension (JSON). Pre-app singleton (driven by ESingletonStage). */
class MAHO_CONFIG_API FConfig final : public TExtension<ESingletonStage, FConfig>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FConfig>;
	FConfig() = default;
};

} // namespace Maho
