#pragma once

// Json plugin — nlohmann/json, exposed as a header-only alias. Consumers include
// this header to get FJsonValue (= nlohmann::json).
#include <nlohmann/json.hpp>

namespace Maho
{
namespace Json
{
using FJsonValue = nlohmann::json;
} // namespace Json
} // namespace Maho
