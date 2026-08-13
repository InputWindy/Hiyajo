#include <Core/Extension/Resource/ResourceIO.h>

#include <Core/Extension/Resource/MeshModelCodec.h>
#include <Core/Extension/Resource/TextureImageCodec.h>
#include <Core/Extension/Resource/ResourceCasset.h>

#include <Core/System/Utf8Path.h>

#include <cctype>
#include <filesystem>
#include <fstream>

namespace Maho
{

// ── FCassetPackageImporter ─────────────────────────────────────

bool FCassetPackageImporter::MatchesSourcePath(const std::string& SourcePath) const
{
	const std::size_t Dot = SourcePath.find_last_of('.');
	if (Dot == std::string::npos) return SourcePath.find(".casset") != std::string::npos;
	std::string Ext = SourcePath.substr(Dot);
	for (char& Ch : Ext) Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
	return Ext == ".casset";
}

bool FCassetPackageImporter::ApplyBulkData(
	FResourceSystem& Manager,
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk)
{
	if (!ResourceCasset::IsCassetBinaryFile(Bulk.Bytes.data(), Bulk.Bytes.size()))
	{
		MAHO_CORE_ERROR("FCassetPackageImporter: not a casset file '{}'", Config.SourcePath);
		return false;
	}

	std::unordered_set<std::string> LoadingFilePaths;
	std::string PkgName = Manager.LoadPackageFromBinary(
		Config.SourcePath,
		Bulk.Bytes.data(),
		Bulk.Bytes.size(),
		LoadingFilePaths);

	return !PkgName.empty();
}

// ── Texture Import/Export helpers ──────────────────────────────

namespace
{

void CopyFileToDestination(const std::string& Src, const std::string& Dst, bool bOverwrite)
{
	namespace fs = std::filesystem;
	std::error_code ErrorCode;

	const fs::path DestParent = PathFromUtf8(Dst).parent_path();
	if (!DestParent.empty())
		fs::create_directories(DestParent, ErrorCode);

	if (fs::exists(PathFromUtf8(Dst), ErrorCode) && !bOverwrite)
		return;

	auto CopyOptions = fs::copy_options::overwrite_existing;
	fs::copy_file(PathFromUtf8(Src), PathFromUtf8(Dst), CopyOptions, ErrorCode);
}

template <typename TTexture>
bool ImportTextureCpu(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	TTexture& Resource)
{
	const bool bModelPrepared = Bulk.PreparedKind == EResourceBulkPreparedKind::Model
		&& Bulk.Prepared;
	const bool bTexturePrepared = Bulk.PreparedKind == EResourceBulkPreparedKind::TextureImage
		&& Bulk.Prepared;

	if (bTexturePrepared)
	{
		const auto* Image = static_cast<const FDecodedImage*>(Bulk.Prepared.get());
		FDecodedImage Copy = *Image;
		Resource.SetCpuImage(Copy.Dimension, Copy.Format, Copy.Width, Copy.Height,
			Copy.Depth, Copy.ArrayLayers, Copy.MipCount, Copy.bSRGB, std::move(Copy.Pixels));
	}
	else if (!Bulk.Bytes.empty())
	{
		FDecodedImage Image;
		if (!TextureImageCodec::DecodeFromMemory(Bulk.Bytes.data(), Bulk.Bytes.size(), Config.SourcePath, Image))
			return false;
		Resource.SetCpuImage(Image.Dimension, Image.Format, Image.Width, Image.Height,
			Image.Depth, Image.ArrayLayers, Image.MipCount, Image.bSRGB, std::move(Image.Pixels));
	}
	else
	{
		return false;
	}

	Resource.MarkDirty();
	return true;
}

template <typename TTexture>
bool ExportTextureCpu(
	FResourceExportConfig& Config,
	const TTexture& Resource)
{
	if (!Config.bOverwrite)
	{
		namespace fs = std::filesystem;
		std::error_code ErrorCode;
		if (fs::exists(PathFromUtf8(Config.DestinationPath), ErrorCode) && !ErrorCode)
			return false;
	}

	if (Resource.HasSerializedSource())
	{
		CopyFileToDestination(Resource.GetSourcePath(), Config.DestinationPath, Config.bOverwrite);
		return true;
	}

	return TextureImageCodec::EncodeToFile(Resource, Config.DestinationPath, Config.bOverwrite);
}

} // namespace

// ── TResourceIOTraits<FTexture2D> ──────────────────────────────

bool TResourceIOTraits<FTexture2D>::MatchesSourcePath(const std::string& SourcePath)
{
	const std::string Ext = TextureImageCodec::GetExtensionLower(SourcePath);
	return TextureImageCodec::IsRasterExtension(Ext)
		|| TextureImageCodec::IsKtx2Extension(Ext);
}

bool TResourceIOTraits<FTexture2D>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	FTexture2D& Resource,
	FResourceSystem* Manager)
{
	(void)Manager;
	return ImportTextureCpu(Config, Bulk, Resource);
}

bool TResourceIOTraits<FTexture2D>::ExportSource(
	FResourceExportConfig& Config,
	const FTexture2D& Resource)
{
	return ExportTextureCpu(Config, Resource);
}

// ── TResourceIOTraits<FTexture3D> ──────────────────────────────

bool TResourceIOTraits<FTexture3D>::MatchesSourcePath(const std::string& SourcePath)
{
	return TextureImageCodec::PathLooksLikeTexture3D(SourcePath);
}

bool TResourceIOTraits<FTexture3D>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	FTexture3D& Resource,
	FResourceSystem* Manager)
{
	(void)Manager;
	return ImportTextureCpu(Config, Bulk, Resource);
}

bool TResourceIOTraits<FTexture3D>::ExportSource(
	FResourceExportConfig& Config,
	const FTexture3D& Resource)
{
	return ExportTextureCpu(Config, Resource);
}

// ── TResourceIOTraits<FTextureCube> ────────────────────────────

bool TResourceIOTraits<FTextureCube>::MatchesSourcePath(const std::string& SourcePath)
{
	return TextureImageCodec::PathLooksLikeCube(SourcePath);
}

bool TResourceIOTraits<FTextureCube>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	FTextureCube& Resource,
	FResourceSystem* Manager)
{
	(void)Manager;
	return ImportTextureCpu(Config, Bulk, Resource);
}

bool TResourceIOTraits<FTextureCube>::ExportSource(
	FResourceExportConfig& Config,
	const FTextureCube& Resource)
{
	return ExportTextureCpu(Config, Resource);
}

// ── TResourceIOTraits<FTextureCubeArray> ───────────────────────

bool TResourceIOTraits<FTextureCubeArray>::MatchesSourcePath(const std::string& SourcePath)
{
	return TextureImageCodec::PathLooksLikeCubeArray(SourcePath);
}

bool TResourceIOTraits<FTextureCubeArray>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	FTextureCubeArray& Resource,
	FResourceSystem* Manager)
{
	(void)Manager;
	return ImportTextureCpu(Config, Bulk, Resource);
}

bool TResourceIOTraits<FTextureCubeArray>::ExportSource(
	FResourceExportConfig& Config,
	const FTextureCubeArray& Resource)
{
	return ExportTextureCpu(Config, Resource);
}

// ── TResourceIOTraits<FTexture2DArray> ─────────────────────────

bool TResourceIOTraits<FTexture2DArray>::MatchesSourcePath(const std::string& SourcePath)
{
	return TextureImageCodec::PathLooksLikeTexture2DArray(SourcePath);
}

bool TResourceIOTraits<FTexture2DArray>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	FTexture2DArray& Resource,
	FResourceSystem* Manager)
{
	(void)Manager;
	return ImportTextureCpu(Config, Bulk, Resource);
}

bool TResourceIOTraits<FTexture2DArray>::ExportSource(
	FResourceExportConfig& Config,
	const FTexture2DArray& Resource)
{
	return ExportTextureCpu(Config, Resource);
}

// ── TResourceIOTraits<FPrefab> ─────────────────────────────────

bool TResourceIOTraits<FPrefab>::MatchesSourcePath(const std::string& SourcePath)
{
	return MeshModelCodec::MatchesModelSourcePath(SourcePath);
}

bool TResourceIOTraits<FPrefab>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	FPrefab& Resource,
	FResourceSystem* Manager)
{
	FDecodedModelScene Scene;
	if (!MeshModelCodec::DecodeFromMemory(Bulk.Bytes.data(), Bulk.Bytes.size(), Config.SourcePath, Scene))
		return false;

	if (!Manager)
		return false;

	return MeshModelCodec::ApplyDecodedModelScene(
		std::move(Scene),
		*Manager,
		Config.PackagePath,
		Resource,
		nullptr);
}

bool TResourceIOTraits<FPrefab>::ExportSource(
	FResourceExportConfig& Config,
	const FPrefab& Resource)
{
	(void)Config;
	(void)Resource;
	return false;
}

} // namespace Maho
