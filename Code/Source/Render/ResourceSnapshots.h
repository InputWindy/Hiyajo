#pragma once

/**
 * CPU snapshots: game-thread resource data -> render-thread proxy upload.
 * Project-side asset enums live in Resource/TextureEnums.h and
 * Resource/AnimationKey.h; the core render system never includes this header.
 */

#include "Resource/AnimationKey.h"
#include "Resource/TextureEnums.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Maho
{

// ── CPU snapshots (Game-thread submit to render thread) ──

struct FTextureCpuSnapshot
{
	std::string CatalogKey;
	std::uint64_t Generation = 0;
	ETextureDimension Dimension = ETextureDimension::Tex2D;
	ETexturePixelFormat PixelFormat = ETexturePixelFormat::Unknown;
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
	std::uint32_t Depth = 1;
	std::uint32_t ArrayLayers = 1;
	std::uint32_t MipCount = 1;
	bool bSRGB = true;
	std::vector<std::uint8_t> Pixels;
};

struct FMeshCpuSnapshot
{
	std::string CatalogKey;
	std::uint64_t Generation = 0;
	std::vector<std::uint8_t> InterleavedVertices;
	std::vector<std::uint32_t> Indices;
	std::uint32_t VertexStride = 0;
	std::uint32_t VertexCount = 0;
	bool bHasSkinning = false;
};

struct FSkeletonCpuSnapshot
{
	std::string CatalogKey;
	std::uint64_t Generation = 0;
	std::uint32_t BoneCount = 0;
	std::vector<std::string> BoneNames;
	std::vector<std::int32_t> ParentIndex;
	std::vector<float> InverseBindPose;
};

struct FAnimationTrackSnapshot
{
	std::string TargetBoneName;
	std::int32_t TargetBoneIndex = -1;
	std::vector<FAnimationKey> Keys;
};

struct FAnimationCpuSnapshot
{
	std::string CatalogKey;
	std::uint64_t Generation = 0;
	std::string SkeletonCatalogKey;
	float DurationSeconds = 0.f;
	std::vector<FAnimationTrackSnapshot> Tracks;
};

} // namespace Maho
