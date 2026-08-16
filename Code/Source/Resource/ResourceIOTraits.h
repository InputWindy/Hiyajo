#pragma once

/**
 * Concrete TResourceIOTraits specializations for game asset types.
 * These used to live in the engine core; they moved here together with the
 * concrete F* asset classes and the CPU codecs.
 */

#include <ResourceIO.h>
#include "Resource/ResourceTypes.h"

namespace Maho
{

/**
 * Prepared bulk-data type tag (project-side). Used when a CPU codec pre-decodes
 * raw bytes into a typed payload before the importer consumes it. The engine
 * core stays type-erased (FResourceBulkData carries only raw Bytes).
 */
enum class EResourceBulkPreparedType : std::uint8_t
{
	None = 0,
	Model = 1,
	TextureImage = 2,
};

template <>
struct TResourceIOTraits<FTexture2D>
{
	static constexpr EAssetType GetType()
	{
		return EAssetType::Texture2D;
	}
	static constexpr const char* TypeNames[] = { "Texture2D", "Texture", "FTexture2D", "FTexture" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(FResourceImportConfig& Config, FResourceBulkData& Bulk, FTexture2D& Resource, FResourceSystem* Manager = nullptr);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const FTexture2D& Resource);
};

template <>
struct TResourceIOTraits<FTexture3D>
{
	static constexpr EAssetType GetType()
	{
		return EAssetType::Texture3D;
	}
	static constexpr const char* TypeNames[] = { "Texture3D", "FTexture3D" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(FResourceImportConfig& Config, FResourceBulkData& Bulk, FTexture3D& Resource, FResourceSystem* Manager = nullptr);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const FTexture3D& Resource);
};

template <>
struct TResourceIOTraits<FTextureCube>
{
	static constexpr EAssetType GetType()
	{
		return EAssetType::TextureCube;
	}
	static constexpr const char* TypeNames[] = { "TextureCube", "FTextureCube", "Cubemap" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(FResourceImportConfig& Config, FResourceBulkData& Bulk, FTextureCube& Resource, FResourceSystem* Manager = nullptr);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const FTextureCube& Resource);
};

template <>
struct TResourceIOTraits<FTextureCubeArray>
{
	static constexpr EAssetType GetType()
	{
		return EAssetType::TextureCubeArray;
	}
	static constexpr const char* TypeNames[] = { "TextureCubeArray", "FTextureCubeArray" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(FResourceImportConfig& Config, FResourceBulkData& Bulk, FTextureCubeArray& Resource, FResourceSystem* Manager = nullptr);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const FTextureCubeArray& Resource);
};

template <>
struct TResourceIOTraits<FTexture2DArray>
{
	static constexpr EAssetType GetType()
	{
		return EAssetType::Texture2DArray;
	}
	static constexpr const char* TypeNames[] = { "Texture2DArray", "FTexture2DArray" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(FResourceImportConfig& Config, FResourceBulkData& Bulk, FTexture2DArray& Resource, FResourceSystem* Manager = nullptr);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const FTexture2DArray& Resource);
};

template <>
struct TResourceIOTraits<FPrefab>
{
	static constexpr EAssetType GetType()
	{
		return EAssetType::Prefab;
	}
	static constexpr const char* TypeNames[] = { "Prefab", "FPrefab", "Model", "MeshScene" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(FResourceImportConfig& Config, FResourceBulkData& Bulk, FPrefab& Resource, FResourceSystem* Manager = nullptr);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const FPrefab& Resource);
};

} // namespace Maho
