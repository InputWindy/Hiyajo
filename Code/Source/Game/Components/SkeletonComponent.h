#pragma once

#include "Game/Components/ComponentCommon.h"

#include <type_traits>

namespace Maho
{

struct FSkeletonComponent
{
	char SkeletonPath[ECSComponentAssetPathMax] = {};

	[[nodiscard]] bool IsValid() const
	{
		return SkeletonPath[0] != '\0';
	}
};

} // namespace Maho

static_assert(std::is_trivially_copyable_v<Maho::FSkeletonComponent>, "FSkeletonComponent must be trivially copyable");
