#pragma once

#include "Game/Components/ComponentCommon.h"

#include <type_traits>

namespace Maho
{

struct FStaticMeshComponent
{
	char MeshPath[ECSComponentAssetPathMax] = {};

	[[nodiscard]] bool IsValid() const
	{
		return MeshPath[0] != '\0';
	}
};

} // namespace Maho

static_assert(std::is_trivially_copyable_v<Maho::FStaticMeshComponent>, "FStaticMeshComponent must be trivially copyable");
