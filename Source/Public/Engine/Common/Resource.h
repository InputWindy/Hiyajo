#pragma once

// Resource — typed async resource system (engine Common, TSingleton). Async
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
// The host drives the fixed lifecycle: Initiate() starts the IO thread, Tick()
// is called every frame to apply ready transfers on the game thread, Shutdown()
// stops the thread and clears the catalog.
#include <Core/Singleton.h>
#include <Engine/ThreadedServer.h>

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

// ── generic config bases — VIRTUAL paths, resolved via FPaths ──

struct FImportConfig
{
	std::string SourcePath;   // virtual source path, e.g. "Raw/mesh.fbx" — the asset
	                          // path (catalog key) is derived by stripping the extension
};

struct FExportConfig
{
	std::string DestinationPath;   // explicit physical path, e.g. "C:/Out/mesh.fbx"
};

// ── resource base — typed resources derive from this ──

class FResource
{
public:
	virtual ~FResource() = default;

	explicit FResource(std::string InPath) : Path(std::move(InPath)) {}

	[[nodiscard]] std::string_view GetPath() const { return Path; }

private:
	std::string Path;
};

// ── importer / exporter — specialize per resource type (receive raw bytes) ──

template <typename TResource>
struct TResourceImporter;   // undefined — specialize per resource type

template <typename TResource>
struct TResourceExporter;   // undefined — specialize per resource type

/**
 * Resource system — async transfer server + typed import/export.
 *
 *   Resource::FResourceSystem::Get().Import<FMesh>({ "Raw/mesh.fbx" });
 *   const Resource::FResource* R = Resource::FResourceSystem::Get().TryLoad("Raw/mesh");
 *
 * The async transfer machinery (handle / bulk data / pending queue) is fully
 * internal — the header only forward-declares it. Importers see std::span /
 * std::vector of raw bytes.
 */
class FResourceSystem
	: public TSingleton<FResourceSystem>
	, public FThreadedServer
{
public:
	/** Process-unique accessor — defined in Resource.cpp (in Maho.dll). */
	static FResourceSystem& Get();

	~FResourceSystem() override;

	// The fixed single-Layer lifecycle (host-driven).
	void Initiate(int Argc, char** Argv) override;
	void Shutdown() override;

	/** Apply ready transfers on the game thread — call once per frame. */
	void Tick();

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
	friend TSingleton<FResourceSystem>;
	// ISingleton needs TSingleton's default ctor; FThreadedServer's thread is
	// started lazily in Initiate.
	FResourceSystem();

	bool EnqueueImport(
		std::string SourcePath,
		std::string AssetPath,
		std::function<void(std::span<const std::uint8_t>)> OnBulkReady);
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

// ── template definitions ──

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

	// Copy the source path FIRST — EnqueueImport's argument and the lambda's
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
