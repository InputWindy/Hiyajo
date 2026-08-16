#pragma once

/**
 * Concrete game asset types (project-defined). The engine core only knows the
 * FResource base + FResourceSystem framework; each game defines its own asset
 * classes here and registers them with the resource manager via factories.
 */

#include <ResourceSystem.h>
#include "Resource/TextureEnums.h"
#include "Resource/AnimationKey.h"
#include <Core/Misc/Archive.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Maho
{

// ── Texture types ──────────────────────────────────────────────

class FTexture : public FResource
{
public:
	FTexture(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] ETextureDimension GetDimension() const
	{
		return Dimension;
	}
	[[nodiscard]] ETexturePixelFormat GetPixelFormat() const
	{
		return PixelFormat;
	}
	[[nodiscard]] std::uint32_t GetWidth() const
	{
		return Width;
	}
	[[nodiscard]] std::uint32_t GetHeight() const
	{
		return Height;
	}
	[[nodiscard]] std::uint32_t GetDepth() const
	{
		return Depth;
	}
	[[nodiscard]] std::uint32_t GetArrayLayers() const
	{
		return ArrayLayers;
	}
	[[nodiscard]] std::uint32_t GetMipCount() const
	{
		return MipCount;
	}
	[[nodiscard]] bool IsSRGB() const
	{
		return bSRGB;
	}
	[[nodiscard]] const std::vector<std::uint8_t>& GetPixels() const
	{
		return Pixels;
	}
	[[nodiscard]] std::vector<std::uint8_t>& GetPixelsMutable()
	{
		return Pixels;
	}

	[[nodiscard]] const std::vector<std::uint8_t>& GetSerializedSourceBytes() const
	{
		return SerializedSourceBytes;
	}
	[[nodiscard]] const std::string& GetSerializedSourceHint() const
	{
		return SerializedSourceHint;
	}
	[[nodiscard]] bool HasSerializedSource() const
	{
		return !SerializedSourceBytes.empty();
	}
	void SetSerializedSource(std::string Hint, std::vector<std::uint8_t> Bytes);
	void ClearSerializedSource();

	void SetCpuImage(
		ETextureDimension InDimension,
		ETexturePixelFormat InFormat,
		std::uint32_t InWidth,
		std::uint32_t InHeight,
		std::uint32_t InDepth,
		std::uint32_t InArrayLayers,
		std::uint32_t InMipCount,
		bool bInSRGB,
		std::vector<std::uint8_t> InPixels);

	void Serialize(FArchive& Ar) override;

protected:
	ETextureDimension Dimension = ETextureDimension::Tex2D;
	ETexturePixelFormat PixelFormat = ETexturePixelFormat::Unknown;
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
	std::uint32_t Depth = 1;
	std::uint32_t ArrayLayers = 1;
	std::uint32_t MipCount = 1;
	bool bSRGB = true;
	std::vector<std::uint8_t> Pixels;
	std::string SerializedSourceHint;
	std::vector<std::uint8_t> SerializedSourceBytes;
};

class FTexture2D : public FTexture
{
public:
	FTexture2D(std::string InName, EAssetType InType, std::string InSourcePath);
};

class FTexture3D : public FTexture
{
public:
	FTexture3D(std::string InName, EAssetType InType, std::string InSourcePath);
};

class FTextureCube : public FTexture
{
public:
	FTextureCube(std::string InName, EAssetType InType, std::string InSourcePath);
};

class FTextureCubeArray : public FTexture
{
public:
	FTextureCubeArray(std::string InName, EAssetType InType, std::string InSourcePath);
};

class FTexture2DArray : public FTexture
{
public:
	FTexture2DArray(std::string InName, EAssetType InType, std::string InSourcePath);
};

// ── Material ───────────────────────────────────────────────────

class FMaterial : public FResource
{
public:
	FMaterial(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] const std::string& GetBaseColorTexture() const
	{
		return BaseColorPath;
	}
	void SetBaseColorTexture(std::string Path)
	{
		BaseColorPath = std::move(Path);
	}
	[[nodiscard]] const std::string& GetNormalTexture() const
	{
		return NormalPath;
	}
	void SetNormalTexture(std::string Path)
	{
		NormalPath = std::move(Path);
	}
	[[nodiscard]] const std::string& GetMetallicRoughnessTexture() const
	{
		return MetallicRoughnessPath;
	}
	void SetMetallicRoughnessTexture(std::string Path)
	{
		MetallicRoughnessPath = std::move(Path);
	}
	[[nodiscard]] const std::string& GetOcclusionTexture() const
	{
		return OcclusionPath;
	}
	void SetOcclusionTexture(std::string Path)
	{
		OcclusionPath = std::move(Path);
	}
	[[nodiscard]] const std::string& GetEmissiveTexture() const
	{
		return EmissivePath;
	}
	void SetEmissiveTexture(std::string Path)
	{
		EmissivePath = std::move(Path);
	}

	[[nodiscard]] std::vector<std::string> GetReferencePaths() const override;

	void Serialize(FArchive& Ar) override;

	float BaseColorFactor[4] = {1.f, 1.f, 1.f, 1.f};
	float MetallicFactor = 0.f;
	float RoughnessFactor = 1.f;
	float EmissiveFactor[3] = {0.f, 0.f, 0.f};

protected:
	std::string BaseColorPath;
	std::string NormalPath;
	std::string MetallicRoughnessPath;
	std::string OcclusionPath;
	std::string EmissivePath;
};

// ── Static Mesh ────────────────────────────────────────────────

class FStaticMesh : public FResource
{
public:
	FStaticMesh(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] const std::string& GetMaterial() const
	{
		return MaterialPath;
	}
	void SetMaterial(std::string Path)
	{
		MaterialPath = std::move(Path);
	}
	[[nodiscard]] const std::vector<float>& GetPositions() const
	{
		return Positions;
	}
	[[nodiscard]] const std::vector<float>& GetNormals() const
	{
		return Normals;
	}
	[[nodiscard]] const std::vector<float>& GetUVs() const
	{
		return UVs;
	}
	[[nodiscard]] const std::vector<std::uint32_t>& GetIndices() const
	{
		return Indices;
	}

	void SetCpuGeometry(
		std::vector<float> InPositions,
		std::vector<float> InNormals,
		std::vector<float> InUVs,
		std::vector<std::uint32_t> InIndices);

	[[nodiscard]] std::vector<std::string> GetReferencePaths() const override;

	void Serialize(FArchive& Ar) override;

protected:
	std::string MaterialPath;
	std::vector<float> Positions;
	std::vector<float> Normals;
	std::vector<float> UVs;
	std::vector<std::uint32_t> Indices;
};

// ── Skeleton / Animation ───────────────────────────────────────

struct FSkeletonBone
{
	std::string Name;
	std::int32_t ParentIndex = -1;
	float BindLocal[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1};
};

struct FAnimationTrack
{
	std::string TargetBoneName;
	std::vector<FAnimationKey> Keys;
};

class FSkeleton : public FResource
{
public:
	FSkeleton(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] const std::vector<FSkeletonBone>& GetBones() const
	{
		return Bones;
	}
	void SetBones(std::vector<FSkeletonBone> InBones);

	void Serialize(FArchive& Ar) override;

protected:
	std::vector<FSkeletonBone> Bones;
};

class FAnimation : public FResource
{
public:
	FAnimation(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] const std::string& GetSkeleton() const
	{
		return SkeletonPath;
	}
	void SetSkeleton(std::string Path);
	[[nodiscard]] float GetDurationSeconds() const
	{
		return DurationSeconds;
	}
	void SetDurationSeconds(float Seconds);
	[[nodiscard]] const std::vector<FAnimationTrack>& GetTracks() const
	{
		return Tracks;
	}
	void SetTracks(std::vector<FAnimationTrack> InTracks);

	[[nodiscard]] std::vector<std::string> GetReferencePaths() const override;

	void Serialize(FArchive& Ar) override;

protected:
	std::string SkeletonPath;
	float DurationSeconds = 0.f;
	std::vector<FAnimationTrack> Tracks;
};

// ── AnimationGraph / Prefab ────────────────────────────────────

class FAnimationGraph : public FResource
{
public:
	FAnimationGraph(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] const std::string& GetDocumentJson() const
	{
		return DocumentJson;
	}
	void SetDocumentJson(std::string Json)
	{
		DocumentJson = std::move(Json);
	}

	void Serialize(FArchive& Ar) override;

protected:
	std::string DocumentJson;
};

class FPrefab : public FResource
{
public:
	FPrefab(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] const std::string& GetDocumentJson() const
	{
		return DocumentJson;
	}
	void SetDocumentJson(std::string Json)
	{
		DocumentJson = std::move(Json);
	}

	void Serialize(FArchive& Ar) override;

protected:
	std::string DocumentJson;
};

} // namespace Maho
