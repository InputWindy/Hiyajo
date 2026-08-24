#pragma once

// Json — nlohmann/json exposed as a header-only alias (engine Common). Consumers
// include this header to get FJsonValue (= nlohmann::json); nlohmann is an
// ENGINE third-party (Build/CMake/MahoDependencies).
#include <nlohmann/json.hpp>

namespace Maho
{
namespace Json
{
using FJsonValue = nlohmann::json;
} // namespace Json
} // namespace Maho
