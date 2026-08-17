#pragma once

#include "JsonApi.h"
#include <Toolkit.h>

#include <nlohmann/json.hpp>

namespace Maho
{

namespace Json
{

/** The JSON value type (nlohmann/json). Consumers include this header to use it. */
using FJsonValue = nlohmann::json;

/** JSON serialization extension (nlohmann/json). Pre-app toolkit (driven by EToolStage). */
class MAHO_JSON_API FJson : public TExtension<EToolStage, FJson>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

protected:
	friend TSingleton<FJson>;
	FJson() = default;
};

} // namespace Json

} // namespace Maho
