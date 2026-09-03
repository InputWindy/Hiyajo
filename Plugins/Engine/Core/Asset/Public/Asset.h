#pragma once

// Asset ?asset registry (engine Common, engine layer). FAssetPath logical
// "/Game/..." ?FAssetData metadata. Scan() indexes a content dir, Find()
// resolves a logical path, Resolve() maps it to a physical file via FPaths
// (engine Common root alias), Load() reads raw bytes.
#include <Maho.h>

#include "AssetApi.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Maho
{
namespace Asset
{

/** Asset type (extensible; inferred from the on-disk extension). */
enum class EAssetType : std::uint8_t
{
	Unknown = 0,
	Material,
	Texture,
};

/**
 * Logical asset path ?no extension, no object part (no UObject system).
 *   const FAssetPath P = FAssetPath("/Game/Materials/M_Metal");
 */
class FAssetPath
{
public:
	FAssetPath() = default;
	explicit FAssetPath(std::string InPath) : Path(std::move(InPath)) {}

	[[nodiscard]] std::string_view GetPath() const { return Path; }

	[[nodiscard]] bool operator==(const FAssetPath& Other) const { return Path == Other.Path; }
	[[nodiscard]] bool operator!=(const FAssetPath& Other) const { return Path != Other.Path; }
	[[nodiscard]] bool operator<(const FAssetPath& Other) const { return Path < Other.Path; }

private:
	std::string Path;
};

/** Resolved asset metadata. */
struct FAssetData
{
	FAssetPath Path;
	EAssetType Type = EAssetType::Unknown;
	std::filesystem::path File;                       // physical file (Content/Materials/M_Metal.material)
	std::vector<FAssetPath> Dependencies;             // lazily resolved (deserialization fills this)
};

class FAssetRegistry;

/** Global asset registry accessor - returns FAssetRegistry* (cross-DLL via function). */
MAHO_ASSET_API FAssetRegistry* GetAssetRegistry();

/**
 * Asset registry: logical path ?metadata. Scan() walks a content directory
 * and indexes every asset file; Find() resolves a logical path.
 */
class FAssetRegistry
	: public FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>
{
public:
	MAHO_DECLARE_LAYER(FAssetRegistry, "Asset.dll");

	/**
	 * Recursively index a content directory.
	 * Content/Materials/M_Metal.material ?/Game/Materials/M_Metal (Material).
	 */
	void Scan(const std::filesystem::path& ContentDir, std::string_view MountAlias = "Game");

	/** Look up an asset by logical path; nullptr when absent. */
	[[nodiscard]] const FAssetData* Find(const FAssetPath& Path) const;

	/** Resolve a logical path to its physical file via FPaths (the mount alias is a root). */
	[[nodiscard]] std::filesystem::path Resolve(const FAssetPath& Path) const;

	/** Load an asset file's raw bytes; nullopt when missing or unreadable. */
	[[nodiscard]] std::optional<std::vector<std::uint8_t>> Load(const FAssetPath& Path) const;

	[[nodiscard]] std::size_t GetAssetCount() const;

private:
	// -- engine pipeline stages (scheduler-only) --
	void PreInitialize(FEngineBase&) override {}
	void Initialize(FEngineBase& Engine) override;
	void PostInitialize(FEngineBase&) override {}
	void PreShutdown(FEngineBase&) override {}
	void Shutdown(FEngineBase& Engine) override;
	void PostShutdown(FEngineBase&) override {}

protected:
	FAssetRegistry() = default;

	mutable std::mutex Mutex;
	std::map<std::string, FAssetData> Assets;   // logical path string ?metadata
};

} // namespace Asset
} // namespace Maho
