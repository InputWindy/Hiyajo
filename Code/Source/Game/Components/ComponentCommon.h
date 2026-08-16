#pragma once

/**
 * Shared ECS component string-size limits. Components that carry asset paths or
 * names include this header.
 */

#include <cstddef>

namespace Maho
{

constexpr std::size_t ECSComponentAssetPathMax = 256;
constexpr std::size_t ECSComponentNameMax = 64;

} // namespace Maho
