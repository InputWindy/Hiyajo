#pragma once

#include "JsonApi.h"
#include <Maho.h>
#include <Engine/Tool.h>

#include <nlohmann/json.hpp>

namespace Maho
{

namespace Json
{

/** The JSON value type (nlohmann/json). Consumers include this header to use it. */
using FJsonValue = nlohmann::json;

/** JSON serialization extension (nlohmann/json). A plain singleton type provider. */
class MAHO_JSON_API FJsonTool : public Maho::TTool<FJsonTool>
{
public:
	/** Aggregate identity tags: base + this Tool's own (empty for now). */
	using FTags = TCatch<typename Maho::TTool<FJsonTool>::FTags, TTypeList<>>::Type;

	// header-only — no lifecycle, no stage.
};

} // namespace Json

} // namespace Maho
