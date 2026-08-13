#pragma once

/**
 * Resource extension: FResource asset types, catalog, package IO.
 *
 * DOTS-aligned: FResource is a plain C++ base class (virtual dtor for OOP dispatch).
 * No UObject, no FObjectRef, no FSoftObjectPath, no GC pool, no MAHO_OBJECT macros.
 * Asset paths are plain std::string. Catalog is unordered_map<string, Ref<FResource>>.
 */

#include <Core/DependsPack.h>
#include <Core/Export.h>
#include <Core/Json.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/Serialization/Archive.h>
#include <Core/Server/TransferHandle.h>
#include <Core/System/RefCounted.h>
#include <Core/TypeList.h>
#include <Core/Concurrent/AsyncTask.h>
#include <Render/ResourceSnapshots.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Maho
{

// ── Shared enums ───────────────────────────────────────────────

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

enum class EAssetLoadState : std::uint8_t
{
	Invalid = 0,
	Pending,
	Ready,
	Failed
};

enum class EAssetType : std::uint8_t
{
	Unknown = 0,
	Texture2D,
	Texture3D,
	TextureCube,
	TextureCubeArray,
	Texture2DArray,
	Mesh,
	Material,
	Skeleton,
	Animation,
	AnimationGraph,
	Prefab,
	Shader,
	Audio,
	Data,
	Level,
	World,
};

// Keep old alias for backward compat during transition
using EResourceType = EAssetType;
using EResourceLoadState = EAssetLoadState;

// ── FResource: base class for all asset types ──────────────────

class MAHO_API FResource : public TRefCounted<FResource>
{
public:
	FResource() = default;
	explicit FResource(
		std::string InName,
		EAssetType InType,
		std::string InSourcePath);

	virtual ~FResource() = default;

	[[nodiscard]] const std::string& GetName() const { return Name; }
	void SetName(std::string InName) { Name = std::move(InName); }

	[[nodiscard]] EAssetType GetType() const { return Type; }
	[[nodiscard]] const std::string& GetSourcePath() const { return SourcePath; }
	[[nodiscard]] EAssetLoadState GetLoadState() const { return LoadState; }

	void MarkCpuReady() { LoadState = EAssetLoadState::Ready; bDirty = false; }
	void MarkDirty() { bDirty = true; }
	void ClearDirty() { bDirty = false; }
	[[nodiscard]] bool IsDirty() const { return bDirty; }

	[[nodiscard]] std::uint64_t GetContentGeneration() const { return ContentGeneration; }

	/**
	 * Return asset paths (std::string) that this asset depends on.
	 * Used by the .casset codec to populate the DEPS chunk for cascade saves.
	 */
	[[nodiscard]] virtual std::vector<std::string> GetReferencePaths() const
	{
		return {};
	}

	/**
	 * Bidirectional binary serialization via FArchive (UE-compatible pattern).
	 * Each subclass overrides this to save/load its CPU fields.
	 */
	virtual void Serialize(FArchive& Ar) { Ar.SerializeString(Name); Ar.SerializeString(SourcePath); }

protected:
	friend class FResourceSystem;

	std::string Name;
	EAssetType Type = EAssetType::Unknown;
	std::string SourcePath;
	EAssetLoadState LoadState = EAssetLoadState::Pending;
	bool bDirty = false;
	std::uint64_t ContentGeneration = 0;
};

// ── Texture types ──────────────────────────────────────────────

class MAHO_API FTexture : public FResource
{
public:
	FTexture(
		std::string InName,
		EAssetType InType,
		std::string InSourcePath);

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

	[[nodiscard]] const std::vector<std::uint8_t>& GetSerializedSourceBytes() const { return SerializedSourceBytes; }
	[[nodiscard]] const std::string& GetSerializedSourceHint() const { return SerializedSourceHint; }
	[[nodiscard]] bool HasSerializedSource() const { return !SerializedSourceBytes.empty(); }
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

class MAHO_API FTexture2D : public FTexture
{
public:
	FTexture2D(std::string InName, EAssetType InType, std::string InSourcePath);
};

class MAHO_API FTexture3D : public FTexture
{
public:
	FTexture3D(std::string InName, EAssetType InType, std::string InSourcePath);
};

class MAHO_API FTextureCube : public FTexture
{
public:
	FTextureCube(std::string InName, EAssetType InType, std::string InSourcePath);
};

class MAHO_API FTextureCubeArray : public FTexture
{
public:
	FTextureCubeArray(std::string InName, EAssetType InType, std::string InSourcePath);
};

class MAHO_API FTexture2DArray : public FTexture
{
public:
	FTexture2DArray(std::string InName, EAssetType InType, std::string InSourcePath);
};

// ── Material ───────────────────────────────────────────────────

class MAHO_API FMaterial : public FResource
{
public:
	FMaterial(std::string InName, EAssetType InType, std::string InSourcePath);

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

class MAHO_API FStaticMesh : public FResource
{
public:
	FStaticMesh(std::string InName, EAssetType InType, std::string InSourcePath);

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

class MAHO_API FSkeleton : public FResource
{
public:
	FSkeleton(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] const std::vector<FSkeletonBone>& GetBones() const { return Bones; }
	void SetBones(std::vector<FSkeletonBone> InBones);

	void Serialize(FArchive& Ar) override;

protected:
	std::vector<FSkeletonBone> Bones;
};

class MAHO_API FAnimation : public FResource
{
public:
	FAnimation(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] const std::string& GetSkeleton() const { return SkeletonPath; }
	void SetSkeleton(std::string Path);
	[[nodiscard]] float GetDurationSeconds() const { return DurationSeconds; }
	void SetDurationSeconds(float Seconds);
	[[nodiscard]] const std::vector<FAnimationTrack>& GetTracks() const { return Tracks; }
	void SetTracks(std::vector<FAnimationTrack> InTracks);

	[[nodiscard]] std::vector<std::string> GetReferencePaths() const override;

	void Serialize(FArchive& Ar) override;

protected:
	std::string SkeletonPath;
	float DurationSeconds = 0.f;
	std::vector<FAnimationTrack> Tracks;
};

// ── AnimationGraph / Prefab ────────────────────────────────────

class MAHO_API FAnimationGraph : public FResource
{
public:
	FAnimationGraph(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] const std::string& GetDocumentJson() const { return DocumentJson; }
	void SetDocumentJson(std::string Json) { DocumentJson = std::move(Json); }

	void Serialize(FArchive& Ar) override;

protected:
	std::string DocumentJson;
};

class MAHO_API FPrefab : public FResource
{
public:
	FPrefab(std::string InName, EAssetType InType, std::string InSourcePath);

	[[nodiscard]] const std::string& GetDocumentJson() const { return DocumentJson; }
	void SetDocumentJson(std::string Json) { DocumentJson = std::move(Json); }

	void Serialize(FArchive& Ar) override;

protected:
	std::string DocumentJson;
};

// ── BulkData / IO types ────────────────────────────────────────

enum class EResourceBulkPreparedKind : std::uint8_t
{
	None = 0,
	Model = 1,
	TextureImage = 2,
};

struct FResourceBulkData
{
	std::string SourcePath;
	std::vector<std::uint8_t> Bytes;
	EResourceBulkPreparedKind PreparedKind = EResourceBulkPreparedKind::None;
	std::shared_ptr<void> Prepared;
};

enum class EResourceIOMode : std::uint8_t
{
	Async = 0,
	Sync = 1,
};

struct FResourceImportConfig
{
	std::string PackagePath;
	std::string ObjectName;
	std::string SourcePath;
	EAssetType TypeHint = EAssetType::Unknown;
	EResourceIOMode Mode = EResourceIOMode::Async;
};

struct FResourceExportConfig
{
	std::string DestinationPath;
	bool bOverwrite = true;
	EResourceIOMode Mode = EResourceIOMode::Async;
};

/** Per-package container: a collection of FResource* sharing the same .casset file. */
struct FResourcePackage
{
	std::string Name;
	std::string FilePath;
	std::uint32_t Flags = 0;
	std::vector<FResource*> Objects;
};

// ── FResourceSystem ────────────────────────────────────────────

class MAHO_API IResourceImporter;
class MAHO_API IResourceExporter;
class FResourceServer;

class MAHO_API FResourceSystem final
	: public IEngineExtension
{
public:
	FResourceSystem();
	~FResourceSystem() override;

	FResourceSystem(const FResourceSystem&) = delete;
	FResourceSystem& operator=(const FResourceSystem&) = delete;

	/** Find a loaded asset by path (e.g. "/Game/Textures/T_Base"). */
	template <typename T = FResource>
	[[nodiscard]] Ref<T> Find(const std::string& AssetPath) const
	{
		std::string Key = NormalizeResourceVirtualPath(AssetPath);
		auto It = Catalog.find(Key);
		if (It == Catalog.end())
			return Ref<T>();
		return Ref<T>(static_cast<T*>(It->second.Get()));
	}

	/** Load package from disk. Returns true if already cached or load started. */
	[[nodiscard]] bool TryLoad(const std::string& AssetPath, bool bAsync = true);

	template <typename TImporter>
	[[nodiscard]] bool Import(FResourceImportConfig Config, std::string& OutAssetPath);

	template <typename TExporter>
	[[nodiscard]] bool Export(FResourceExportConfig Config, const std::string& SourcePath);

	/** Save a loaded package to .casset (disk path derived from PackagePath via FPaths). */
	[[nodiscard]] bool SavePackage(
		const std::string& PackagePath,
		bool bSaveDependencies = true);

	/** Build "PackagePath.ObjectName" catalog key (pure string utility). */
	[[nodiscard]] static std::string MakeAssetCatalogKey(
		const std::string& PackagePath,
		const std::string& ObjectName);

	/** Register a new resource (takes ownership via Ref). Used by codecs/importers. */
	[[nodiscard]] bool RegisterResource(Ref<FResource> Resource, const std::string& PackagePath = {});

	/** Remove a resource from the catalog by pointer. Used for import rollback. */
	bool UnregisterResource(FResource* Resource);

private:
	template <typename TResource>
	friend class TResourceImporter;
	friend class FCassetPackageImporter;
	friend class FResource;
	friend class FEditorLayer;

	[[nodiscard]] bool Initialize();
	void Shutdown();
	void PrepareForExit();

	[[nodiscard]] bool IsInitialized() const;

	const char* GetName() const override { return "Resource"; }
	bool ExecuteStage(EEngineStage Stage) override;
	[[nodiscard]] bool IsIdle() const override;

	struct FPendingIO
	{
		FTransferHandle Handle;
		std::string AssetPath;
		FResourceImportConfig Config;
		std::unique_ptr<IResourceImporter> Importer;
	};

	[[nodiscard]] static std::string NormalizePackageName(std::string Name);
	[[nodiscard]] static std::string NormalizeSourcePath(std::string Path);
	[[nodiscard]] static std::string NormalizeResourceVirtualPath(const std::string& VirtualPath);
	[[nodiscard]] static std::string MakeObjectNameFromSource(const std::string& SourcePath);

	void UnregisterResourcesInPackage(const std::string& PackagePath);
	void AbortFailedImport(FResource& Resource);
	void CancelPendingImport(FResource* Resource);
	void ProcessReadyIO();
	void TickSavePackage();
	void FinalizeSavePackageSuccess();
	void FinalizeSavePackageFailure();

	[[nodiscard]] bool RegisterOwnedResource(const std::string& PackagePath, FResource* Resource);

	void Flush(const std::string& AssetPath);
	void FlushAll();

	// ── EditorLayer access (friend) ──
	[[nodiscard]] bool EnqueueSavePackage(
		const std::string& PackagePath,
		bool bSaveDependencies = true);
	[[nodiscard]] bool IsSavePackageBusy() const;
	[[nodiscard]] float GetSavePackageProgress() const;
	[[nodiscard]] const std::string& GetSavePackageStatusText() const;
	[[nodiscard]] EAssetLoadState GetLoadState(const std::string& AssetPath) const;
	void ForEachRegisteredResource(
		const std::function<void(const std::string& CatalogKey, FResource& Resource)>& Fn) const;

	[[nodiscard]] bool EnqueueImport(
			std::unique_ptr<IResourceImporter> Importer,
			FResourceImportConfig Config,
			std::string& OutAssetPath);

	template <typename TResource>
	[[nodiscard]] bool ApplyTypedBulkData(FResourceImportConfig& Config, FResourceBulkData& Bulk);

	[[nodiscard]] bool HasActiveServer() const;
	[[nodiscard]] FTransferHandle RequestBulkLoad(const std::string& SourcePath);
	void ReleaseBulkLoad(FTransferHandle Handle);
	[[nodiscard]] bool TakeBulkData(FTransferHandle Handle, FResourceBulkData& OutBulk);

	[[nodiscard]] bool SavePackageInternal(
			const std::string& PackagePath,
			bool bSaveDependencies,
			std::unordered_set<std::string>& SavingPackageNames);

	[[nodiscard]] FResourcePackage* FindOrCreatePackage(const std::string& PackagePath, const std::string& FilePath = {});

	[[nodiscard]] std::string LoadPackage(const std::string& FilePath);
	[[nodiscard]] std::string LoadPackageInternal(
		const std::string& FilePath,
		std::unordered_set<std::string>& LoadingFilePaths);
	[[nodiscard]] std::string LoadPackageFromBinary(
		const std::string& FilePath,
		const std::uint8_t* FileBytes,
		std::size_t FileSize,
		std::unordered_set<std::string>& LoadingFilePaths);

	/** Replicate UPackage model: loaded per-package collections. */
	std::unordered_map<std::string, std::unique_ptr<FResourcePackage>> Packages;

	std::unique_ptr<FResourceServer> Server;

	/** Catalog: "PackagePath/ObjectName" → Ref<FResource>. */
	std::unordered_map<std::string, Ref<FResource>> Catalog;

	std::unordered_map<std::string, FPendingIO> PendingIO;
	bool bAcceptingNewWork = true;

	struct FAsyncSaveJob
	{
		std::string PackagePath;
		std::string OutPath;
		std::vector<std::uint8_t> Document;
		std::shared_ptr<std::vector<std::uint8_t>> FileBytes;
		std::shared_ptr<std::atomic<bool>> bOk;
		std::unique_ptr<FAsyncTask> Task;
		float Progress = 0.f;
		std::string StatusText;
		bool bActive = false;
		bool bCompressStarted = false;
	};
	FAsyncSaveJob AsyncSave;
};

namespace Detail
{
[[nodiscard]] MAHO_API FResourceSystem* GetResourceSystem();
}

} // namespace Maho
