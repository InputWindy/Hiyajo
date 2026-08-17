#pragma once

#include "ResourceApi.h"
#include <Core/Async/ThreadedServer.h>
#include <Core/Core.h>
#include <Paths.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Maho
{

namespace Resource
{

// Internal implementation types — defined in the .cpp, only forward-declared.
struct FTransferState;
struct FBulkData;
class FTransferHandle;

// ───────────────────────────────────────────────────────────────────────
// Generic config bases — VIRTUAL paths, resolved via FPaths.
// ───────────────────────────────────────────────────────────────────────

struct FImportConfig
{
	std::string SourcePath;   // virtual source path, e.g. "Raw/mesh.fbx" — the asset path (catalog key) is derived by stripping the extension
};

struct FExportConfig
{
	std::string DestinationPath;   // destination path on disk, e.g. "C:/Out/mesh.fbx" — explicit, no virtual resolution
};

// ───────────────────────────────────────────────────────────────────────
// Resource base — typed resources derive from this.
// ───────────────────────────────────────────────────────────────────────

class MAHO_RESOURCE_API FResource
{
public:
	virtual ~FResource() = default;

	explicit FResource(std::string InPath) : Path(std::move(InPath)) {}

	[[nodiscard]] std::string_view GetPath() const { return Path; }

private:
	std::string Path;
};

// ───────────────────────────────────────────────────────────────────────
// Importer / exporter — partial-specialize per resource type. They receive
// raw bulk data (std::span / std::vector) and only decode / encode.
//
//   template <>
//   struct TResourceImporter<FMeshResource>
//   {
//       using FConfig = FMeshImportConfig;
//       static bool Import(const FConfig&, std::span<const std::uint8_t>, FMeshResource&);
//   };
//
//   template <>
//   struct TResourceExporter<FMeshResource>
//   {
//       using FConfig = FMeshExportConfig;
//       static bool Export(const FConfig&, const FMeshResource&, std::vector<std::uint8_t>&);
//   };
// ───────────────────────────────────────────────────────────────────────

template <typename TResource>
struct TResourceImporter;   // undefined — specialize per resource type

template <typename TResource>
struct TResourceExporter;   // undefined — specialize per resource type

/**
 * Resource system — async transfer server + typed import/export.
 *
 *   FResourceSystem::Get().Import<FMeshResource>({ "Raw/mesh.fbx" });
 *   const FResource* R = FResourceSystem::Get().TryLoad("Raw/mesh");
 *
 * The async transfer machinery (handle / bulk data / pending queue) is fully
 * internal — the header only forward-declares it. Importers see std::span /
 * std::vector of raw bytes.
 */
class MAHO_RESOURCE_API FResourceSystem
	: public TExtension<EEngineStage, FResourceSystem>
	, public FThreadedServer
{
public:
	~FResourceSystem() override;

	[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

	/** Async import; OnDone receives the registered resource or nullptr. */
	template <typename TResource>
	bool Import(typename TResourceImporter<TResource>::FConfig Config, std::function<void(const FResource*)> OnDone = {});

	/** Sync export; TResource's exporter specialization encodes into bytes. */
	template <typename TResource>
	bool Export(typename TResourceExporter<TResource>::FConfig Config, std::string_view AssetPath);

	/** Find a loaded resource; nullptr when absent. */
	[[nodiscard]] const FResource* Find(std::string_view AssetPath) const;

	/** Try to load: Find; nullptr when not loaded yet. */
	[[nodiscard]] const FResource* TryLoad(std::string_view AssetPath);

protected:
	[[nodiscard]] const char* GetThreadName() const override;

protected:
	friend TSingleton<FResourceSystem>;
	FResourceSystem();

	class FImpl;
	std::unique_ptr<FImpl> Impl;

	bool EnqueueImport(
		std::string SourcePath,
		std::string AssetPath,
		std::function<void(std::span<const std::uint8_t>)> OnBulkReady);
	const FResource* RegisterResource(std::string AssetPath, std::unique_ptr<FResource> Resource);
	FTransferHandle RequestLoad(std::string Path);
	void ProcessReadyIO();
	static bool WriteBytes(std::string_view PhysicalPath, std::span<const std::uint8_t> Bytes);
};

// ── template definitions ──

template <typename TResource>
bool FResourceSystem::Import(typename TResourceImporter<TResource>::FConfig Config, std::function<void(const FResource*)> OnDone)
{
	if (Config.SourcePath.empty())
	{
		return false;
	}

	// Derive the asset path (catalog key) from the source path — strip the extension.
	const std::size_t Dot = Config.SourcePath.find_last_of('.');
	const std::string AssetPath = (Dot == std::string::npos)
		? Config.SourcePath
		: Config.SourcePath.substr(0, Dot);

	return EnqueueImport(
		Config.SourcePath,
		AssetPath,
		[this, Config = std::move(Config), AssetPath, OnDone = std::move(OnDone)](std::span<const std::uint8_t> Bytes) mutable
		{
			// Decode on the game thread once the bulk data is ready.
			const FResource* Raw = nullptr;
			auto Resource = std::make_unique<TResource>(AssetPath);
			if (TResourceImporter<TResource>::Import(Config, Bytes, *Resource))
			{
				Raw = RegisterResource(AssetPath, std::move(Resource));
			}
			if (OnDone)
			{
				OnDone(Raw);
			}
		});
}

template <typename TResource>
bool FResourceSystem::Export(typename TResourceExporter<TResource>::FConfig Config, std::string_view AssetPath)
{
	const FResource* Resource = Find(AssetPath);
	if (Resource == nullptr)
	{
		return false;
	}
	const TResource* Typed = dynamic_cast<const TResource*>(Resource);
	if (Typed == nullptr)
	{
		return false;
	}

	std::vector<std::uint8_t> Bytes;
	if (!TResourceExporter<TResource>::Export(Config, *Typed, Bytes))
	{
		return false;
	}

	return WriteBytes(Config.DestinationPath, Bytes);
}

} // namespace Resource

} // namespace Maho
