#pragma once

/**
 * Private CPU model codec for ResourceIO (Assimp when MAHO_WITH_ASSIMP).
 * Decode -> FDecodedModelScene -> Apply* to F* + FPrefab. Never touches RHI.
 * DOTS-aligned: no UObject, no GC, no UPackage.
 */

#include "ResourceTypes.h"
#include "TextureImageCodec.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Maho
{

enum class EModelAxis : std::uint8_t
{
	X = 0,
	Y = 1,
	Z = 2,
	NegativeX = 3,
	NegativeY = 4,
	NegativeZ = 5,
};

enum class EModelHandedness : std::uint8_t
{
	Left = 0,
	Right = 1,
};

class FResourceSystem;
class FPrefab;

struct FDecodedCoordinateSystem
{
	EModelAxis Up = EModelAxis::Y;
	EModelAxis Forward = EModelAxis::Z;
	EModelHandedness Handedness = EModelHandedness::Right;
};

struct FDecodedModelMetadata
{
	FDecodedCoordinateSystem CoordinateSystem;
	float UnitScale = 1.f;
	std::unordered_map<std::string, std::string> ExtraKeys;
};

struct FDecodedBoneWeight
{
	std::uint32_t BoneIndex = 0;
	float Weight = 0.f;
};

struct FDecodedMesh
{
	std::string Name;
	std::vector<float> Positions;
	std::vector<float> Normals;
	std::vector<float> UVs;
	std::vector<std::uint32_t> Indices;
	std::vector<std::vector<FDecodedBoneWeight>> BoneWeights;
	std::int32_t MaterialIndex = -1;
};

struct FDecodedBone
{
	std::string Name;
	std::int32_t ParentIndex = -1;
	float BindLocal[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1};
};

struct FDecodedSkeleton
{
	std::vector<FDecodedBone> Bones;
	[[nodiscard]] bool IsEmpty() const { return Bones.empty(); }
};

struct FDecodedAnimKey
{
	float Time = 0.f;
	float Translation[3] = {0, 0, 0};
	float Rotation[4] = {0, 0, 0, 1};
	float Scale[3] = {1, 1, 1};
};

struct FDecodedAnimTrack
{
	std::string TargetBoneName;
	std::vector<FDecodedAnimKey> Keys;
};

struct FDecodedAnimation
{
	std::string Name;
	float DurationSeconds = 0.f;
	std::vector<FDecodedAnimTrack> Tracks;
};

struct FDecodedTextureRef
{
	std::string SlotName;
	std::string SourcePath;
	std::vector<std::uint8_t> EmbeddedBytes;
};

struct FDecodedMaterial
{
	std::string Name;
	float BaseColorFactor[4] = {1.f, 1.f, 1.f, 1.f};
	float MetallicFactor = 0.f;
	float RoughnessFactor = 1.f;
	float EmissiveFactor[3] = {0.f, 0.f, 0.f};
	std::vector<FDecodedTextureRef> Textures;
};

struct FDecodedSceneNode
{
	std::string Name;
	std::int32_t ParentIndex = -1;
	float LocalTransform[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1};
	std::int32_t MeshIndex = -1;
};

/** Temporary CPU bag from Decode - not a UObject, never saved. */
struct FDecodedModelScene
{
	std::string SourcePathHint;
	FDecodedModelMetadata Metadata;
	std::vector<FDecodedMesh> Meshes;
	FDecodedSkeleton Skeleton;
	std::vector<FDecodedAnimation> Animations;
	std::vector<FDecodedMaterial> Materials;
	std::vector<FDecodedSceneNode> Nodes;
};

/** Worker-thread texture decode result for Prefab Apply (game thread). */
struct FPreparedTextureImage
{
	std::string CacheKey;
	std::string DecodeHint;
	bool bForceLinear = false;
	FDecodedImage Image;
	std::vector<std::uint8_t> SerializedSourceBytes;
};

/** Assimp + texture decode prepared off the game thread. */
struct FPreparedModelImport
{
	FDecodedModelScene Scene;
	std::unordered_map<std::string, FPreparedTextureImage> Textures;
};

namespace MeshModelCodec
{

[[nodiscard]] std::string GetExtensionLower(std::string_view Path);
[[nodiscard]] bool IsModelExtension(std::string_view Ext);
[[nodiscard]] bool MatchesModelSourcePath(std::string_view Path);

[[nodiscard]] bool DecodeFromMemory(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePathHint,
	FDecodedModelScene& Out);

[[nodiscard]] bool PrepareModelImport(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePathHint,
	FPreparedModelImport& Out);

/**
 * Apply decoded scene: create F* assets in FResourceSystem under the given package path.
 * Order: Materials(+textures) -> Meshes -> Skeleton -> Animations -> Prefab JSON.
 */
[[nodiscard]] bool ApplyDecodedModelScene(
	FDecodedModelScene&& Scene,
	FResourceSystem& Resources,
	const std::string& PackagePath,
	FPrefab& Prefab,
	const std::unordered_map<std::string, FPreparedTextureImage>* PreparedTextures = nullptr);

} // namespace MeshModelCodec
} // namespace Maho
