#pragma once

// Resource â€?typed async resource system (engine Common, TSingleton). Async
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
//   Resource::FResourceSystem::Get().Import<FMesh>({ "Raw/mesh.fbx" },
//       [](const Resource::FResource* R) { /* loaded, game thread */ });
//
// The host drives the fixed lifecycle: Initialize() starts the IO thread, Tick()
// is called every frame to apply ready transfers on the game thread, Shutdown()
// stops the thread and clears the catalog.
#include <Core/Singleton.h>
#include <Core/ThreadedServer.h>
#include <Maho.h>

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

// Internal implementation types â€?defined in the .cpp, only forward-declared.
struct FTransferState;
struct FBulkData;
class FTransferHandle;

// â”€â”€ generic config bases â€?VIRTUAL paths, resolved via FPaths â”€â”€

struct FImportConfig
{
	std::string SourcePath;   // virtual source path, e.g. "Raw/mesh.fbx" â€?the asset
		                          // path (catalog key) is derived by stripping the extension
};

struct FExportConfig
{
	std::string DestinationPath;   // explicit physical path, e.g. "C:/Out/mesh.fbx"
};

// â”€â”€ resource base â€?typed resources derive from this â”€â”€

class FResource
{
public:
	virtual ~FResource() = default;

	explicit FResource(std::string InPath) : Path(std::move(InPath)) {}

	[[nodiscard]] std::string_view GetPath() const { return Path; }

private:
	std::string Path;
};

// â”€â”€ importer / exporter â€?specialize per resource type (receive raw bytes) â”€â”€

template <typename TResource>
struct TResourceImporter;   // undefined â€?specialize per resource type

template <typename TResource>
struct TResourceExporter;   // undefined â€?specialize per resource type

/**
 * Resource system â€?async transfer server + typed import/export.
 *
 *   Resource::FResourceSystem::Get().Import<FMesh>({ "Raw/mesh.fbx" });
 *   const Resource::FResource* R = Resource::FResourceSystem::Get().TryLoad("Raw/mesh");
 *
 * The async transfer machinery (handle / bulk data / pending queue) is fully
 * internal â€?the header only forward-declares it. Importers see std::span /
 * std::vector of raw bytes.
 */
class FResourceSystem
	: public TSingleton<FResourceSystem>
	, public FThreadedServer
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Process-unique accessor â€?defined in Resource.cpp (in Resource.dll). */
	static FResourceSystem& Get();

	~FResourceSystem() override;

	// The fixed single-Layer lifecycle (host-driven).
	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;

	/** Apply ready transfers on the game thread â€?call once per frame. */
	void Tick();

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

protected:
	friend TSingleton<FResourceSystem>;
	// ISingleton needs TSingleton's default ctor; FThreadedServer's thread is
	// started lazily in Initialize.
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

// â”€â”€ template definitions â”€â”€

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

	// Copy the source path FIRST â€?EnqueueImport's argument and the lambda's
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

	// Encode on the CALLER (game) thread â€?the exporter reads the catalog resource
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
