#pragma once

// Concrete game asset types (project-defined). The engine core only knows the
// FResource base + FResourceSystem framework (async IO thread + FName catalog);
// each game defines its own asset classes here and specializes TResourceImporter
// / TResourceExporter (raw bytes codecs) for them. No engine-side constants here.

#include "Resource.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Maho
{
namespace Resource
{

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

// -- FTexture2D: CPU pixel container (decoded asset, not a GPU handle) --

class FTexture2D : public FResource
{
public:
	explicit FTexture2D(std::string InPath) : FResource(std::move(InPath)) {}

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

private:
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

// ── Material ───────────────────────────────────────────────────

class FMaterial : public FResource
{
public:
	explicit FMaterial(std::string InPath) : FResource(std::move(InPath)) {}

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
	explicit FStaticMesh(std::string InPath) : FResource(std::move(InPath)) {}

	[[nodiscard]] const std::string& GetMaterial() const { return MaterialPath; }
	void SetMaterial(std::string Path) { MaterialPath = std::move(Path); }
	[[nodiscard]] const std::vector<float>& GetPositions() const { return Positions; }
	[[nodiscard]] const std::vector<float>& GetNormals() const { return Normals; }
	[[nodiscard]] const std::vector<float>& GetUVs() const { return UVs; }
	[[nodiscard]] const std::vector<std::uint32_t>& GetIndices() const { return Indices; }

	void SetCpuGeometry(
		std::vector<float> InPositions,
		std::vector<float> InNormals,
		std::vector<float> InUVs,
		std::vector<std::uint32_t> InIndices);

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
	explicit FSkeleton(std::string InPath) : FResource(std::move(InPath)) {}

	[[nodiscard]] const std::vector<FSkeletonBone>& GetBones() const { return Bones; }
	void SetBones(std::vector<FSkeletonBone> InBones) { Bones = std::move(InBones); }

protected:
	std::vector<FSkeletonBone> Bones;
};

class FAnimation : public FResource
{
public:
	explicit FAnimation(std::string InPath) : FResource(std::move(InPath)) {}

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

// ── AnimationGraph / Prefab (document holders) ─────────────────

class FAnimationGraph : public FResource
{
public:
	explicit FAnimationGraph(std::string InPath) : FResource(std::move(InPath)) {}

	[[nodiscard]] const std::string& GetDocumentJson() const { return DocumentJson; }
	void SetDocumentJson(std::string Json) { DocumentJson = std::move(Json); }

protected:
	std::string DocumentJson;
};

class FPrefab : public FResource
{
public:
	explicit FPrefab(std::string InPath) : FResource(std::move(InPath)) {}

	[[nodiscard]] const std::string& GetDocumentJson() const { return DocumentJson; }
	void SetDocumentJson(std::string Json) { DocumentJson = std::move(Json); }

protected:
	std::string DocumentJson;
};

// -- TResourceImporter<FTexture2D>: decode raw raster bytes into a CPU image --
// FConfig must be visible to call sites (Import<T> reads it), so the
// specialization is declared here; the body lives in the private .cpp.
template <>
struct TResourceImporter<FTexture2D>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FTexture2D& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FTexture2D>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Export(const FConfig& Config, const FTexture2D& Resource, std::vector<std::uint8_t>& OutBytes);
};

// -- value-type importer/exporters: round-trip a compact binary layout --
// (These types have no standalone file codec; the codec mirrors their fields.)

template <>
struct TResourceImporter<FMaterial>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FMaterial& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FMaterial>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Export(const FConfig& Config, const FMaterial& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FStaticMesh>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FStaticMesh& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FStaticMesh>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Export(const FConfig& Config, const FStaticMesh& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FSkeleton>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FSkeleton& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FSkeleton>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Export(const FConfig& Config, const FSkeleton& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FAnimation>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FAnimation& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FAnimation>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Export(const FConfig& Config, const FAnimation& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FAnimationGraph>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FAnimationGraph& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FAnimationGraph>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Export(const FConfig& Config, const FAnimationGraph& Resource, std::vector<std::uint8_t>& OutBytes);
};

template <>
struct TResourceImporter<FPrefab>
{
	using FConfig = FImportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Import(const FConfig& Config, std::span<const std::uint8_t> Bytes, FPrefab& Out, FResourceSystem& System);
};

template <>
struct TResourceExporter<FPrefab>
{
	using FConfig = FExportConfig;
	[[nodiscard]] MAHO_RESOURCE_API static bool Export(const FConfig& Config, const FPrefab& Resource, std::vector<std::uint8_t>& OutBytes);
};

} // namespace Resource
} // namespace Maho
