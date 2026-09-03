#pragma once

// Resource - typed async resource system (engine Common, FEngineLayer). Async
// import/export over a dedicated IO thread (FThreadedServer), catalog keyed by
// FName, virtual paths resolved through FPaths. Importers/exporters are
// user-specialized templates that only decode/encode raw bytes.
//
//   template <>
//   struct Maho::Resource::TResourceImporter<FMesh>
//   {
//       using FConfig = FMeshImportConfig;
//       static bool Import(const FConfig&, std::span<const std::uint8_t>, FMesh&);
//   };
//
//   Resource::GetResourceSystem()->Import<FMesh>({ "Raw/mesh.fbx" },
//       [](const Resource::FResource* R) { /* loaded, game thread */ });
//
// The engine loop drives the lifecycle: Initialize() starts the IO thread,
// Tick(FEngineBase&) is called every frame to apply ready transfers on the
// game thread, Shutdown() stops the thread and clears the catalog.
#include <Core/ThreadedServer.h>
#include <Maho.h>
#include <Engine/Engine.h>

#include "ResourceApi.h"
#include <Core/Delegate.h>
#include <Name.h>

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
class FResource;
class FResourceSystem;
struct FTransferState;
struct FBulkData;
class FTransferHandle;

// -- importer / exporter - specialize per resource type (receive raw bytes) --

template <typename TResource>
struct TResourceImporter;   // undefined - specialize per resource type

template <typename TResource>
struct TResourceExporter;   // undefined - specialize per resource type

/** Transfer completion callback - the upstream (FResourceSystem) builds this and
 *  hands it to a listener when broadcasting an imported/loaded resource; the listener
 *  invokes it after it has finished consuming (e.g. uploaded + mirrored) to report
 *  success/failure. On success the resource system drops the resource's CPU bulk data. */
using FOnTransferDone = std::function<void(bool bSuccess, std::string_view Error)>;

/** GPU fill-back provider - called synchronously (game thread) before an export when a
 *  resource's CPU bulk was dropped (mirrored to GPU). A listener (FRender) dynamic_casts
 *  the resource to its concrete type and decodes the GPU mirror back into the resource's
 *  CPU fields (texture pixels, mesh vertices, ...) - the decode knowledge lives at the
 *  consumer. Returns false when the resource isn't mirrored or the readback failed. */
using FReadbackFn = std::function<bool(const Name::FName& AssetName, FResource& Resource)>;


/** Global resource system accessor - returns FResourceSystem* (cross-DLL via function). */
MAHO_RESOURCE_API FResourceSystem* GetResourceSystem();


// -- generic config bases - VIRTUAL paths, resolved via FPaths --

struct FImportConfig
{
	std::string SourcePath;   // virtual source path, e.g. "Raw/mesh.fbx" - the asset
		                          // path (catalog key) is derived by stripping the extension
};

struct FExportConfig
{
	std::string DestinationPath;   // explicit physical path, e.g. "C:/Out/mesh.fbx"
};

// -- resource base - typed resources derive from this --

class FResource
{
public:
	virtual ~FResource() = default;

	explicit FResource(std::string InPath) : Path(std::move(InPath)) {}
	FResource(FResource&&) = default;
	FResource& operator=(FResource&&) = default;

	[[nodiscard]] std::string_view GetPath() const { return Path; }

	/** Release the resource's CPU bulk payload (e.g. decoded pixels) once a consumer
	 *  (e.g. the render mirror) has finished taking it. Default: no-op - a typed
	 *  resource with a bulky payload overrides this. Called from the game thread. */
	virtual void ReleaseBulk() {}

	/** Whether the CPU bulk payload is currently present. Default: true - a typed
	 *  resource that can be dropped (mirrored to GPU) overrides this and reports
	 *  false after ReleaseBulk(). */
	[[nodiscard]] virtual bool HasBulk() const { return true; }

private:
	std::string Path;
};

/**
 * Resource system - async transfer server + typed import/export.
 *
 *   Resource::GetResourceSystem()->Import<FMesh>({ "Raw/mesh.fbx" });
 *   const Resource::FResource* R = Resource::GetResourceSystem()->TryLoad("Raw/mesh");
 *
 * The async transfer machinery (handle / bulk data / pending queue) is fully
 * internal - the header only forward-declares it. Importers see std::span /
 * std::vector of raw bytes.
 */
class FResourceSystem
	: public FLayer<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>
	, public FThreadedServer
{
public:
	MAHO_DECLARE_LAYER(FResourceSystem, "Resource.dll");

	~FResourceSystem() override;

	/** Broadcast when a resource finishes importing (game thread). A listener (e.g.
	 *  the render mirror) copies the payload to GPU, then invokes Done to report
	 *  success/failure - on success the resource system drops the CPU bulk data.
	 *  The asset is looked up by FName via Find() - no pointer is passed, so nothing
	 *  dangles. */
	TMulticastEvent<void(const Name::FName&, FOnTransferDone)> OnAssetImported;

	/** Broadcast when a resource is unloaded / invalidated (game thread). */
	TMulticastEvent<void(const Name::FName&, FOnTransferDone)> OnAssetUnloaded;

	/** Broadcast when a resource finishes exporting (game thread): (asset FName, success). */
	TMulticastEvent<void(const Name::FName&, bool)> OnAssetExported;

	/** Inject the GPU readback provider (FRender) so Export can restore a dropped CPU
	 *  bulk by reading it back from the mirror. Called once during render init. */
	void SetReadback(FReadbackFn Fn) { Readback = std::move(Fn); }

	/** Async import. On success the resource is registered and OnAssetImported broadcasts
	 *  (asset FName + completion callback). Listeners mirror the payload or act on it; the
	 *  resource system drops the CPU bulk when the listener reports success via Done. */
	template <typename TResource>
	bool Import(typename TResourceImporter<TResource>::FConfig Config);

	/**
	 * Async export; the exporter encodes + writes on the IO thread. Completion is
	 * reported by OnAssetExported.Broadcast on the game thread (after Tick applies it).
	 * The caller must keep the resource alive & unchanged while the export runs.
	 */
	template <typename TResource>
	bool Export(typename TResourceExporter<TResource>::FConfig Config, std::string_view AssetPath);

	/** Find a loaded resource; nullptr when absent. */
	[[nodiscard]] const FResource* Find(std::string_view AssetPath) const;

	/** Mutable find for internal restore (Export's readback); nullptr when absent. */
	[[nodiscard]] FResource* FindMutable(std::string_view AssetPath);

	/** Try to load: Find; nullptr when not loaded yet. */
	[[nodiscard]] const FResource* TryLoad(std::string_view AssetPath);

private:
	// -- engine pipeline stages (scheduler-only) --
	void PreInitialize(FEngineBase&) override {}
	void Initialize(FEngineBase& Engine) override;
	void PostInitialize(FEngineBase&) override {}
	void BeginFrame(FEngineBase&) override {}
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase&) override {}
	void RequestExit(FEngineBase&) override {}
	void PreShutdown(FEngineBase&) override {}
	void Shutdown(FEngineBase& Engine) override;
	void PostShutdown(FEngineBase&) override {}

	FResourceSystem();

	bool EnqueueImport(
		std::string SourcePath,
		std::string AssetPath,
		std::function<void(std::span<const std::uint8_t>)> OnBulkReady);
	/** Encode on the CALLER thread (safe: reads catalog resources synchronously),
	 *  then hand the bytes to the IO thread for WriteBytes; OnDone on game thread. */
	bool EnqueueExport(
		std::vector<std::uint8_t> Bytes,
		std::string DestinationPath,
		Name::FName AssetName);
	const FResource* RegisterResource(std::string AssetPath, std::unique_ptr<FResource> Resource);
	FTransferHandle RequestLoad(std::string Path);
	void ProcessReadyIO();
	static bool WriteBytes(std::string_view PhysicalPath, std::span<const std::uint8_t> Bytes);

	class FImpl;
	std::unique_ptr<FImpl> Impl;

	/** GPU readback provider - set by SetReadback (FRender). */
	FReadbackFn Readback;

	/** Build a transfer-completion callback for the given asset path - on success it
	 *  drops the resource's CPU bulk (ReleaseBulk) under the catalog lock. */
	FOnTransferDone MakeTransferDone(std::string AssetPath);
};

namespace detail
{
	[[nodiscard]] std::size_t FindLastDot(std::string_view Path);
}

// -- template definitions --

template <typename TResource>
bool FResourceSystem::Import(typename TResourceImporter<TResource>::FConfig Config)
{
	if (Config.SourcePath.empty())
	{
		return false;
	}
	const std::size_t Dot = detail::FindLastDot(Config.SourcePath);
	const std::string AssetPath = (Dot == std::string::npos)
		? Config.SourcePath
		: Config.SourcePath.substr(0, Dot);

			// Copy the source path FIRST - EnqueueImport's argument and the lambda's
	// Config-capture are both evaluated for the call, and argument evaluation
	// order isn't guaranteed; reading Config.SourcePath AFTER it was moved into
	// the capture would see an empty string.
	const std::string SourcePath = Config.SourcePath;

	return EnqueueImport(
		SourcePath,
		AssetPath,
		[this, Config = std::move(Config), AssetPath](std::span<const std::uint8_t> Bytes) mutable
		{
			// Decode on the game thread once the bulk data is ready.
			auto Resource = std::make_unique<TResource>(AssetPath);
			if (TResourceImporter<TResource>::Import(Config, Bytes, *Resource, *this))
			{
				RegisterResource(AssetPath, std::move(Resource));
				OnAssetImported.Broadcast(Name::FName(AssetPath), MakeTransferDone(AssetPath));
			}
		});
}

template <typename TResource>
bool FResourceSystem::Export(typename TResourceExporter<TResource>::FConfig Config, std::string_view AssetPath)
{
	// Copy the asset path + config first (the lambda captures by move; argument
	// evaluation order isn't guaranteed, so reading them after the move sees empty).
	const std::string Path(AssetPath);
	FResource* Resource = FindMutable(Path);
	if (Resource == nullptr)
	{
		return false;
	}
	const TResource* Typed = dynamic_cast<const TResource*>(Resource);
	if (Typed == nullptr)
	{
		return false;
	}

	// Encode on the CALLER (game) thread - the exporter reads the catalog resource
	// synchronously here (no cross-thread shared access). Only the disk write is
	// deferred to the IO thread.
	//
	// If the resource's CPU bulk was dropped (imported -> mirrored to GPU -> released),
	// restore it first via the blocking GPU fill-back provider (FRender). This makes the
	// export reflect the current (possibly GPU-processed) data; the provider decodes the
	// GPU mirror directly into the resource's concrete fields.
	if (!Resource->HasBulk() && Readback)
	{
		Readback(Name::FName(Path), *Resource);
	}

	std::vector<std::uint8_t> Bytes;
	if (!TResourceExporter<TResource>::Export(Config, *Typed, Bytes))
	{
		return false;
	}

	return EnqueueExport(std::move(Bytes), Config.DestinationPath, Name::FName(Path));
}

} // namespace Resource
} // namespace Maho
