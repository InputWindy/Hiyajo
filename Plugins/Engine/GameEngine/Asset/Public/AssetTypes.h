#pragma once

// Concrete game asset types (Asset plugin). The Resource plugin only knows the
// FResource base + FResourceSystem framework (async IO thread + FName catalog)
// and the TResourceImporter / TResourceExporter template hooks. This plugin
// defines the concrete asset classes and specializes those templates (raw bytes
// codecs) for them. The casset container (FAssetsResource) lives here too —
// it holds the container header (type/version) + dependency table, and its
// Serialize(FArchive&) is the container codec every concrete asset extends.

#include "AssetApi.h"
#include "Resource.h"

#include <Archive.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Maho
{
namespace Resource
{

// -- asset type / logic path (migrated from the removed Asset plugin) --

/** Asset type (extensible; inferred from the on-disk extension). */
enum class EAssetType : std::uint8_t
{
	Unknown = 0,
	Material,
	Texture,
	StaticMesh,
	Skeleton,
	Animation,
	AnimationGraph,
	Prefab,

	/** First user-defined type; project types extend from here. */
	UserBase,
};

// -- texture-facing enums (asset-side concepts only) --

enum class ETexturePixelFormat : std::uint8_t
{
	Unknown = 0,
	RGBA8,
	RGBA16F,
	RGBA32F,
	R8,
	RG8,
	RGB8,
	BlockCompressed,
	R16F,
	DXT1,
	DXT5,
	BC7,
	Count,
};

enum class ETextureDimension : std::uint8_t
{
	Unknown = 0,
	Tex1D,
	Tex2D,
	Tex3D,
	TexCube,
	Cube = TexCube,
	TexCubeArray,
	Tex2DArray,
	CubeArray = TexCubeArray,
	Count,
};

// ── AssetsResource: casset 容器资源 ─────────────────────────────
// 引擎内置 casset 容器。一个 FAssetsResource 对应磁盘上一个 casset 文件：
// 持有容器 header（type/version）+ 依赖表（引用表）。casset 的 codec 就是
// Serialize(FArchive&) —— 本类写/读容器层; 子类 Serialize 先调本类写容器头，
// 再追加自身业务字段。ResourceManager 只负责把磁盘字节读进 bulkdata 交给
// importer，不关心具体引擎类型。

class FAssetsResource : public FResource, public Archive::ISerialize
{
public:
	FAssetsResource(std::string InPath, EAssetType InType)
		: FResource(std::move(InPath))
		, Type(InType)
	{
	}

	[[nodiscard]] EAssetType GetAssetType() const { return Type; }
	[[nodiscard]] std::uint32_t GetCassetVersion() const { return Version; }

	[[nodiscard]] const std::vector<std::string>& GetDependencies() const { return Dependencies; }
	void SetDependencies(std::vector<std::string> In) { Dependencies = std::move(In); }

	// casset 容器 codec: magic + version + type + 依赖表。
	void Serialize(Archive::FArchive& Ar) override;

protected:
	EAssetType Type;
	std::uint32_t Version = 1;
	std::vector<std::string> Dependencies;
};

// -- FTexture: CPU pixel container (decoded asset, not a GPU handle). Shared by
// every texture-dimension asset type (1D/2D/3D/cube/2D-array/cube-array); each
// dimension is its own derived type for typed Import<T>. --

class FTexture : public FAssetsResource
{
public:
	explicit FTexture(std::string InPath) : FAssetsResource(std::move(InPath), EAssetType::Texture) {}

	// casset payload: 容器头 + 像素快照(解码后 CPU 数据)。
	void Serialize(Archive::FArchive& Ar) override;

	[[nodiscard]] ETextureDimension GetDimension() const { return Dimension; }
	[[nodiscard]] ETexturePixelFormat GetPixelFormat() const { return PixelFormat; }
	[[nodiscard]] std::uint32_t GetWidth() const { return Width; }
	[[nodiscard]] std::uint32_t GetHeight() const { return Height; }
	[[nodiscard]] std::uint32_t GetDepth() const { return Depth; }
	[[nodiscard]] std::uint32_t GetArrayLayers() const { return ArrayLayers; }
	[[nodiscard]] std::uint32_t GetMipCount() const { return MipCount; }
	[[nodiscard]] bool IsSRGB() const { return bSRGB; }
	[[nodiscard]] const std::vector<std::uint8_t>& GetPixels() const { return Pixels; }
	[[nodiscard]] std::vector<std::uint8_t>& GetPixelsMutable() { return Pixels; }

protected:
	// 像素 payload 构造：各维度子类以自身参数转发，字段布局只在
	// FTexture::Serialize 声明一次（构造+持久化共用）。
	FTexture(std::string InPath, ETextureDimension InDimension, ETexturePixelFormat InFormat,
		std::uint32_t InWidth, std::uint32_t InHeight, std::uint32_t InDepth,
		std::uint32_t InArrayLayers, std::uint32_t InMipCount, bool bInSRGB,
		std::vector<std::uint8_t> InPixels)
		: FAssetsResource(std::move(InPath), EAssetType::Texture)
		, Dimension(InDimension), PixelFormat(InFormat), Width(InWidth), Height(InHeight)
		, Depth(InDepth), ArrayLayers(InArrayLayers), MipCount(InMipCount), bSRGB(bInSRGB)
		, Pixels(std::move(InPixels))
	{
	}

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

// One concrete asset type per texture dimension (typed Import<T> / Export<T>);
// each is a thin typed tag over FTexture with a dimension-specialized payload
// constructor (the decode path builds it directly).

class FTexture1D : public FTexture
{
public:
	explicit FTexture1D(std::string InPath) : FTexture(std::move(InPath)) {}
	FTexture1D(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InWidth,
		std::uint32_t InArrayLayers, std::uint32_t InMipCount, bool bInSRGB,
		std::vector<std::uint8_t> InPixels)
		: FTexture(std::move(InPath), ETextureDimension::Tex1D, InFormat, InWidth, 1, 1, InArrayLayers, InMipCount, bInSRGB, std::move(InPixels))
	{
	}
};

class FTexture2D : public FTexture
{
public:
	explicit FTexture2D(std::string InPath) : FTexture(std::move(InPath)) {}
	FTexture2D(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InWidth,
		std::uint32_t InHeight, std::uint32_t InMipCount, bool bInSRGB,
		std::vector<std::uint8_t> InPixels)
		: FTexture(std::move(InPath), ETextureDimension::Tex2D, InFormat, InWidth, InHeight, 1, 1, InMipCount, bInSRGB, std::move(InPixels))
	{
	}
};

class FTexture3D : public FTexture
{
public:
	explicit FTexture3D(std::string InPath) : FTexture(std::move(InPath)) {}
	FTexture3D(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InWidth,
		std::uint32_t InHeight, std::uint32_t InDepth, std::uint32_t InMipCount, bool bInSRGB,
		std::vector<std::uint8_t> InPixels)
		: FTexture(std::move(InPath), ETextureDimension::Tex3D, InFormat, InWidth, InHeight, InDepth, 1, InMipCount, bInSRGB, std::move(InPixels))
	{
	}
};

class FTextureCube : public FTexture
{
public:
	explicit FTextureCube(std::string InPath) : FTexture(std::move(InPath)) {}
	FTextureCube(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InSize,
		std::uint32_t InMipCount, bool bInSRGB, std::vector<std::uint8_t> InPixels)
		: FTexture(std::move(InPath), ETextureDimension::TexCube, InFormat, InSize, InSize, 1, 6, InMipCount, bInSRGB, std::move(InPixels))
	{
	}
};

class FTexture2DArray : public FTexture
{
public:
	explicit FTexture2DArray(std::string InPath) : FTexture(std::move(InPath)) {}
	FTexture2DArray(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InWidth,
		std::uint32_t InHeight, std::uint32_t InArrayLayers, std::uint32_t InMipCount, bool bInSRGB,
		std::vector<std::uint8_t> InPixels)
		: FTexture(std::move(InPath), ETextureDimension::Tex2DArray, InFormat, InWidth, InHeight, 1, InArrayLayers, InMipCount, bInSRGB, std::move(InPixels))
	{
	}
};

class FTextureCubeArray : public FTexture
{
public:
	explicit FTextureCubeArray(std::string InPath) : FTexture(std::move(InPath)) {}
	FTextureCubeArray(std::string InPath, ETexturePixelFormat InFormat, std::uint32_t InSize,
		std::uint32_t InArrayLayers, std::uint32_t InMipCount, bool bInSRGB,
		std::vector<std::uint8_t> InPixels)
		: FTexture(std::move(InPath), ETextureDimension::TexCubeArray, InFormat, InSize, InSize, 1, InArrayLayers, InMipCount, bInSRGB, std::move(InPixels))
	{
	}
};

// -- Pure-value asset containers (fields copied from the Hiyajo reference form).
// These are NOT independently file-imported by the current engine core: in the
// reference implementation they are the typed sub-objects produced by model /
// prefab decode (or document holders). The importer specializations for the
// model family (assimp) are TBD - layout is established here regardless.

struct FAnimationKey
{
	float Time = 0.f;
	float Translation[3] = {0, 0, 0};
	float Rotation[4] = {0, 0, 0, 1}; // xyzw
	float Scale[3] = {1, 1, 1};
};

// ── Static Mesh ────────────────────────────────────────────────

class FStaticMesh : public FAssetsResource
{
public:
	explicit FStaticMesh(std::string InPath) : FAssetsResource(std::move(InPath), EAssetType::StaticMesh) {}

	// casset payload: 容器头 + 几何数据。
	void Serialize(Archive::FArchive& Ar) override;

	[[nodiscard]] const std::string& GetMaterial() const { return MaterialPath; }
	[[nodiscard]] const std::vector<float>& GetPositions() const { return Positions; }
	[[nodiscard]] const std::vector<float>& GetNormals() const { return Normals; }
	[[nodiscard]] const std::vector<float>& GetUVs() const { return UVs; }
	[[nodiscard]] const std::vector<std::uint32_t>& GetIndices() const { return Indices; }

protected:

	// 几何 payload 构造：字段布局只在 FStaticMesh::Serialize 声明一次。
	FStaticMesh(std::string InPath, std::string InMaterial,
		std::vector<float> InPositions, std::vector<float> InNormals,
		std::vector<float> InUVs, std::vector<std::uint32_t> InIndices)
		: FAssetsResource(std::move(InPath), EAssetType::StaticMesh)
		, MaterialPath(std::move(InMaterial))
		, Positions(std::move(InPositions)), Normals(std::move(InNormals))
		, UVs(std::move(InUVs)), Indices(std::move(InIndices))
	{
	}
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

class FSkeleton : public FAssetsResource
{
public:
	explicit FSkeleton(std::string InPath) : FAssetsResource(std::move(InPath), EAssetType::Skeleton) {}

	void Serialize(Archive::FArchive& Ar) override;

	[[nodiscard]] const std::vector<FSkeletonBone>& GetBones() const { return Bones; }
	void SetBones(std::vector<FSkeletonBone> InBones) { Bones = std::move(InBones); }

protected:
	std::vector<FSkeletonBone> Bones;
};

class FAnimation : public FAssetsResource
{
public:
	explicit FAnimation(std::string InPath) : FAssetsResource(std::move(InPath), EAssetType::Animation) {}

	void Serialize(Archive::FArchive& Ar) override;

	[[nodiscard]] const std::string& GetSkeleton() const { return SkeletonPath; }
	void SetSkeleton(std::string Path) { SkeletonPath = std::move(Path); }
	[[nodiscard]] float GetDurationSeconds() const { return DurationSeconds; }
	void SetDurationSeconds(float Seconds) { DurationSeconds = Seconds; }
	[[nodiscard]] const std::vector<FAnimationTrack>& GetTracks() const { return Tracks; }
	void SetTracks(std::vector<FAnimationTrack> InTracks) { Tracks = std::move(InTracks); }

protected:
	std::string SkeletonPath;
	float DurationSeconds = 0.f;
	std::vector<FAnimationTrack> Tracks;
};

// ── Material ───────────────────────────────────────────────────

class FMaterial : public FAssetsResource
{
public:
	explicit FMaterial(std::string InPath)
		: FAssetsResource(std::move(InPath), EAssetType::Material)
	{
	}

	[[nodiscard]] const std::string& GetBaseColorTexture() const { return BaseColorPath; }
	void SetBaseColorTexture(std::string Path) { BaseColorPath = std::move(Path); }
	[[nodiscard]] const std::string& GetNormalTexture() const { return NormalPath; }
	void SetNormalTexture(std::string Path) { NormalPath = std::move(Path); }
	[[nodiscard]] const std::string& GetMetallicRoughnessTexture() const { return MetallicRoughnessPath; }
	void SetMetallicRoughnessTexture(std::string Path) { MetallicRoughnessPath = std::move(Path); }
	[[nodiscard]] const std::string& GetOcclusionTexture() const { return OcclusionPath; }
	void SetOcclusionTexture(std::string Path) { OcclusionPath = std::move(Path); }
	[[nodiscard]] const std::string& GetEmissiveTexture() const { return EmissivePath; }
	void SetEmissiveTexture(std::string Path) { EmissivePath = std::move(Path); }

	// casset payload: 先写容器头(casset codec 容器层)，再追加材质业务字段。
	void Serialize(Archive::FArchive& Ar) override
	{
		FAssetsResource::Serialize(Ar);
		Ar << BaseColorPath << NormalPath << MetallicRoughnessPath << OcclusionPath << EmissivePath;
		Ar << BaseColorFactor[0] << BaseColorFactor[1] << BaseColorFactor[2] << BaseColorFactor[3];
		Ar << MetallicFactor << RoughnessFactor;
		Ar << EmissiveFactor[0] << EmissiveFactor[1] << EmissiveFactor[2];
	}

	float BaseColorFactor[4] = { 1.f, 1.f, 1.f, 1.f };
	float MetallicFactor = 0.f;
	float RoughnessFactor = 1.f;
	float EmissiveFactor[3] = { 0.f, 0.f, 0.f };

protected:
	std::string BaseColorPath;
	std::string NormalPath;
	std::string MetallicRoughnessPath;
	std::string OcclusionPath;
	std::string EmissivePath;
};

// ── AnimationGraph / Prefab (document holders) ─────────────────

class FAnimationGraph : public FAssetsResource
{
public:
	explicit FAnimationGraph(std::string InPath)
		: FAssetsResource(std::move(InPath), EAssetType::AnimationGraph)
	{
	}

	[[nodiscard]] const std::string& GetDocumentJson() const { return DocumentJson; }
	void SetDocumentJson(std::string Json) { DocumentJson = std::move(Json); }

	// casset payload: 先写容器头，再追加文档业务字段。
	void Serialize(Archive::FArchive& Ar) override
	{
		FAssetsResource::Serialize(Ar);
		Ar << DocumentJson;
	}

protected:
	std::string DocumentJson;
};

class FPrefab : public FAssetsResource
{
public:
	explicit FPrefab(std::string InPath)
		: FAssetsResource(std::move(InPath), EAssetType::Prefab)
	{
	}

	[[nodiscard]] const std::string& GetDocumentJson() const { return DocumentJson; }
	void SetDocumentJson(std::string Json) { DocumentJson = std::move(Json); }

	// casset payload: 先写容器头，再追加文档业务字段。
	void Serialize(Archive::FArchive& Ar) override
	{
		FAssetsResource::Serialize(Ar);
		Ar << DocumentJson;
	}

protected:
	std::string DocumentJson;
};

// -- TResourceImporter<T: texture>: decode raw raster bytes into a CPU image --
// Every texture-dimension type shares one codec path (the dimension is a payload
// field), so each specialization delegates to the same private helper; the
// bodies live in the private .cpp. FConfig must be visible to call sites
// (Import<T> reads it), so the specializations are declared here.

template <>
struct TResourceImporter<FTexture1D>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FTexture1D& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FTexture1D>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FTexture1D& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FTexture2D>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FTexture2D& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FTexture2D>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FTexture2D& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FTexture3D>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FTexture3D& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FTexture3D>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FTexture3D& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FTextureCube>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FTextureCube& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FTextureCube>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FTextureCube& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FTexture2DArray>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FTexture2DArray& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FTexture2DArray>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FTexture2DArray& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FTextureCubeArray>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FTextureCubeArray& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FTextureCubeArray>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FTextureCubeArray& Resource, std::vector<std::uint8_t>& OutBytes);
};

// -- value-type importer/exporters: round-trip a compact binary layout --
// (These types have no standalone file codec; the codec mirrors their fields.)

template <>
struct TResourceImporter<FMaterial>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FMaterial& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FMaterial>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FMaterial& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FStaticMesh>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FStaticMesh& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FStaticMesh>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FStaticMesh& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FSkeleton>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FSkeleton& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FSkeleton>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FSkeleton& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FAnimation>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FAnimation& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FAnimation>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FAnimation& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FAnimationGraph>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FAnimationGraph& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FAnimationGraph>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FAnimationGraph& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FPrefab>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FPrefab& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FPrefab>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_ASSET_API static bool Export(const FConfig& Config, const FPrefab& Resource, std::vector<std::uint8_t>& OutBytes);
};

} // namespace Resource
} // namespace Maho
