#pragma once

#include "AssetRegistryApi.h"
#include <Engine.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Maho
{

namespace AssetRegistry
{

/** Asset type, inferred from the on-disk file extension. */
enum class EAssetType : std::uint8_t
{
	Unknown = 0,
	Material,
	Texture,
	StaticMesh,
	SkeletalMesh,
	Blueprint,
	Sound,
};

/**
 * Logical asset identifier — no extension, no object (we have no object
 * system). e.g. "/Game/Materials/M_Metal".
 */
class MAHO_ASSETREGISTRY_API FAssetPath
{
public:
	FAssetPath() = default;
	FAssetPath(std::string InPath) : Path(std::move(InPath)) {}

	[[nodiscard]] std::string_view GetPath() const { return Path; }
	[[nodiscard]] bool IsEmpty() const { return Path.empty(); }

	[[nodiscard]] bool operator<(const FAssetPath& Other) const { return Path < Other.Path; }
	[[nodiscard]] bool operator==(const FAssetPath& Other) const { return Path == Other.Path; }

private:
	std::string Path;
};

/** Asset metadata — logical path → physical file + type + dependencies. */
struct MAHO_ASSETREGISTRY_API FAssetData
{
	FAssetPath Path;
	EAssetType Type = EAssetType::Unknown;
	std::filesystem::path File;
	std::vector<FAssetPath> Dependencies;
};

/**
 * Asset registry — scans a Content directory, indexes logical paths.
 * Pre-app toolkit (driven by EToolStage).
 *
 *   FAssetRegistry::Get().Scan(ContentDir);          // "/Game" = ContentDir
 *   const FAssetData* D = FAssetRegistry::Get().Find("/Game/Materials/M_Metal");
 *   D->File;    // Content/Materials/M_Metal.material
 *   D->Type;    // EAssetType::Material
 */
class MAHO_ASSETREGISTRY_API FAssetRegistry final : public TExtension<EToolStage, FAssetRegistry>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

	/** Scan a content directory; its files become "/Game/..." logical paths. */
	void Scan(std::filesystem::path ContentDir);

	/** Look up an asset by logical path; nullptr when absent. */
	[[nodiscard]] const FAssetData* Find(const FAssetPath& Path) const;

	/** Resolve a logical path to its physical file via FPaths ("/Game" root). */
	[[nodiscard]] std::filesystem::path Resolve(const FAssetPath& Path) const;

	/** Load an asset file's raw bytes; nullopt when missing or unreadable. */
	[[nodiscard]] std::optional<std::vector<std::uint8_t>> Load(const FAssetPath& Path) const;

	[[nodiscard]] std::size_t GetAssetCount() const;

private:
	friend TSingleton<FAssetRegistry>;
	FAssetRegistry() = default;

	std::map<FAssetPath, FAssetData> Assets;
};

} // namespace AssetRegistry

} // namespace Maho
