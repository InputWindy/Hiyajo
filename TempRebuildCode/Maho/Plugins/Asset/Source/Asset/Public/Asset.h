#pragma once

#include "AssetApi.h"
#include <Engine.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
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
 * Logical asset path — no extension, no object part (no UObject system).
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
 * Asset registry: logical path → metadata. Scan() walks a content directory
 * and indexes every asset file; Find() resolves a logical path.
 * Pre-app toolkit (driven by EToolStage).
 */
class MAHO_ASSET_API FAssetRegistry final : public TExtension<EToolStage, FAssetRegistry>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

	/**
	 * Recursively index a content directory.
	 * Content/Materials/M_Metal.material → /Game/Materials/M_Metal (Material).
	 */
	void Scan(const std::filesystem::path& ContentDir, std::string_view MountAlias = "Game");

	/** Look up an asset by logical path; nullptr when absent. */
	[[nodiscard]] const FAssetData* Find(const FAssetPath& Path) const;

private:
	friend TSingleton<FAssetRegistry>;
	FAssetRegistry() = default;

	mutable std::mutex Mutex;
	std::map<std::string, FAssetData> Assets;   // logical path string → metadata
};

} // namespace Asset

} // namespace Maho
