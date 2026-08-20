#pragma once

#include "AssetApi.h"
#include <Maho.h>
#include <Engine/PluginTemplates.h>

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

/** Asset plugin's own drive stage —?the host passes it to Execute<Stage>(). */
enum class EAssetStage : std::uint8_t
{
	Init = 0,
	Shutdown,
};

/** Asset type (extensible; inferred from the on-disk extension). */
enum class EAssetType : std::uint8_t
{
	Unknown = 0,
	Material,
	Texture,
};

/**
 * Logical asset path —?no extension, no object part (no UObject system).
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

/**
 * Asset registry: logical path →?metadata. Scan() walks a content directory
 * and indexes every asset file; Find() resolves a logical path.
 */
class MAHO_ASSET_API FAsset : public Maho::TTool<FAsset>
{
public:
	/** Stage dispatch —?called by `scheduler.Execute<EAssetStage, ...>()`. */
	[[nodiscard]] bool ExecuteStage(EAssetStage Stage);

	/**
	 * Recursively index a content directory.
	 * Content/Materials/M_Metal.material →?/Game/Materials/M_Metal (Material).
	 */
	void Scan(const std::filesystem::path& ContentDir, std::string_view MountAlias = "Game");

	/** Look up an asset by logical path; nullptr when absent. */
	[[nodiscard]] const FAssetData* Find(const FAssetPath& Path) const;

	/** Resolve a logical path to its physical file via the mount alias root. */
	[[nodiscard]] std::filesystem::path Resolve(const FAssetPath& Path) const;

	/** Load an asset file's raw bytes; nullopt when missing or unreadable. */
	[[nodiscard]] std::optional<std::vector<std::uint8_t>> Load(const FAssetPath& Path) const;

	[[nodiscard]] std::size_t GetAssetCount() const;

private:
	mutable std::mutex Mutex;
	std::map<std::string, FAssetData> Assets;          // logical path string →?metadata
	std::map<std::string, std::filesystem::path> MountRoots;   // mount alias →?root dir (thread-guarded by Mutex)
};

} // namespace Asset

} // namespace Maho

