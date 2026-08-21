#pragma once

#include "ResourceApi.h"
#include <Engine/Tool.h>

#include <Core/Async/ThreadedServer.h>
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
// Importer / exporter — partial-specialize per resource type.
//   template <>
//   struct TResourceImporter<FMeshResource>
//   {
//       using FConfig = FMeshImportConfig;
//       static bool Import(const FConfig&, std::span<const std::uint8_t>, FMeshResource&);
//   };
// ───────────────────────────────────────────────────────────────────────

template <typename TResource>
struct TResourceImporter;   // undefined — specialize per resource type

template <typename TResource>
struct TResourceExporter;   // undefined — specialize per resource type

/**
 * Resource system — async transfer server + typed import/export.
 *
 * A Tool: plug-in-and-play, self-managed. Import/Export/TryLoad are runtime
 * services called by game code at any point — read and write are public. Its
 * own worker thread (FThreadedServer) does bulk IO; the registry mutex protects
 * the catalog.
 *
 *   FResourceTool::Get().Import<FMeshResource>({ "Raw/mesh.fbx" });
 *   const FResource* R = FResourceTool::Get().TryLoad("Raw/mesh");
 */
class MAHO_RESOURCE_API FResourceTool
	: public Maho::TTool<FResourceTool>
	, public FThreadedServer
{
public:
	~FResourceTool() override;

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

	/** Start the async load worker (No-op when already running). */
	bool Start();

	/** Stop the async load worker and clear the catalog. */
	void Stop();

	/** Poll completed transfers + decode on the calling thread. */
	void Tick();

private:
	friend Maho::TSingleton<FResourceTool>;
	FResourceTool();

	class FImpl;
	std::unique_ptr<FImpl> Impl;

	[[nodiscard]] const char* GetThreadName() const override;

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
bool FResourceTool::Import(typename TResourceImporter<TResource>::FConfig Config, std::function<void(const FResource*)> OnDone)
{
	if (Config.SourcePath.empty())
	{
		return false;
	}

	const std::size_t Dot = Config.SourcePath.find_last_of('.');
	const std::string AssetPath = (Dot == std::string::npos)
		? Config.SourcePath
		: Config.SourcePath.substr(0, Dot);

	return EnqueueImport(
		Config.SourcePath,
		AssetPath,
		[this, Config = std::move(Config), AssetPath, OnDone = std::move(OnDone)](std::span<const std::uint8_t> Bytes) mutable
		{
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
bool FResourceTool::Export(typename TResourceExporter<TResource>::FConfig Config, std::string_view AssetPath)
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
