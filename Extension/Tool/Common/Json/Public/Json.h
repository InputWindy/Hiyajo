#pragma once

#include "JsonApi.h"
#include <Maho.h>
#include <Engine/PluginTemplates.h>

#include <nlohmann/json.hpp>

namespace Maho
{

namespace Json
{

/** The JSON value type (nlohmann/json). Consumers include this header to use it. */
using FJsonValue = nlohmann::json;

/** JSON serialization extension (nlohmann/json). A plain singleton type provider. */
class MAHO_JSON_API FJson : public Maho::TTool<FJson>
{
public:
	// header-only — no lifecycle, no stage.
};

} // namespace Json

} // namespace Maho
