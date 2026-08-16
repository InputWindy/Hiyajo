#pragma once

#include "JsonApi.h"
#include <Engine.h>

#include <nlohmann/json.hpp>

namespace Maho
{

namespace Json
{

/** The JSON value type (nlohmann/json). Consumers include this header to use it. */
using FJsonValue = nlohmann::json;

/** JSON serialization extension (nlohmann/json). Pre-app toolkit (driven by EToolStage). */
class MAHO_JSON_API FJson final : public TExtension<EToolStage, FJson>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FJson>;
	FJson() = default;
};

} // namespace Json

} // namespace Maho
