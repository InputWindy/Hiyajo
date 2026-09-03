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

class FResourceSystem;

/** Global resource system accessor - returns FResourceSystem* (cross-DLL via function). */
MAHO_RESOURCE_API FResourceSystem* GetResourceSystem();

// Internal implementation types - defined in the .cpp, only forward-declared.
struct FTransferState;
struct FBulkData;
class FTransferHandle;

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

	[[nodiscard]] std::string_view GetPath() const { return Path; }

private:
	std::string Path;
};

// -- importer / exporter - specialize per resource type (receive raw bytes) --

template <typename TResource>
struct TResourceImporter;   // undefined - specialize per resource type

template <typename TResource>
struct TResourceExporter;   // undefined - specialize per resource type

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

	/** Async import; OnDone receives the registered resource or nullptr. */
	template <typename TResource>
	bool Import(typename TResourceImporter<TResource>::FConfig Config, std::function<void(const FResource*)> OnDone = {});

	/**
	 * Async export; the exporter encodes + writes on the IO thread. OnDone(bool)
	 * reports success/failure on the game thread (after Tick applies it).
	 * The caller must keep the resource alive & unchanged while the export runs.
	 */
	template <typename TResource>
	bool Export(typename TResourceExporter<TResource>::FConfig Config, std::string_view AssetPath,
		std::function<void(bool)> OnDone = {});

	/** Find a loaded resource; nullptr when absent. */
	[[nodiscard]] const FResource* Find(std::string_view AssetPath) const;

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
		std::function<void(bool)> OnDone);
	const FResource* RegisterResource(std::string AssetPath, std::unique_ptr<FResource> Resource);
	FTransferHandle RequestLoad(std::string Path);
	void ProcessReadyIO();
	static bool WriteBytes(std::string_view PhysicalPath, std::span<const std::uint8_t> Bytes);

	class FImpl;
	std::unique_ptr<FImpl> Impl;
};

namespace detail
{
	[[nodiscard]] std::size_t FindLastDot(std::string_view Path);
}

// -- template definitions --

template <typename TResource>
bool FResourceSystem::Import(typename TResourceImporter<TResource>::FConfig Config, std::function<void(const FResource*)> OnDone)
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
bool FResourceSystem::Export(typename TResourceExporter<TResource>::FConfig Config, std::string_view AssetPath,
	std::function<void(bool)> OnDone)
{
	// Copy the asset path + config first (the lambda captures by move; argument
	// evaluation order isn't guaranteed, so reading them after the move sees empty).
	const std::string Path(AssetPath);
	const FResource* Resource = Find(Path);
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
	std::vector<std::uint8_t> Bytes;
	if (!TResourceExporter<TResource>::Export(Config, *Typed, Bytes))
	{
		return false;
	}

	return EnqueueExport(std::move(Bytes), Config.DestinationPath, std::move(OnDone));
}

} // namespace Resource
} // namespace Maho
