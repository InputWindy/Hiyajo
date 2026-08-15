#pragma once

#include "JsonApi.h"
#include <Engine.h>

namespace Maho
{

/** JSON serialization extension (nlohmann/json). Pre-app singleton (driven by ESingletonStage). */
class MAHO_JSON_API FJson final : public TExtension<ESingletonStage, FJson>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FJson>;
	FJson() = default;
};

} // namespace Maho
