#include "AssetTypes.h"

#include "TextureImageCodec.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace Maho
{
namespace Resource
{

namespace
{
constexpr std::uint32_t CassetMagic = 0x53534143u;
constexpr std::uint32_t CassetVersion = 1;
}

using namespace detail;

// -- FTexture: CPU image helpers --------------------------------------------

namespace detail
{



bool HasCassetMagic(std::span<const std::uint8_t> Bytes)
{
	if (Bytes.size() < sizeof(std::uint32_t))
	{
		return false;
	}
	std::uint32_t Magic = 0;
	std::memcpy(&Magic, Bytes.data(), sizeof(Magic));
	return Magic == CassetMagic;
}

enum class ETextureImportResult
{
	Handled,    // casset 已解码进 Out
	NeedDecode, // 非 casset：调用者用 FDecodedImage 构造具体维度类型
	Failed,
};

ETextureImportResult ImportTexture(std::span<const std::uint8_t> Bytes, FTexture& Out, FDecodedImage& OutImg)
{
	if (Bytes.empty())
	{
		return ETextureImportResult::Failed;
	}
	if (!HasCassetMagic(Bytes))
	{
		return ETextureImportResult::NeedDecode;
	}
	Archive::FMemoryReader Reader(Bytes);
	try
	{
		Out.Serialize(Reader);
		return ETextureImportResult::Handled;
	}
	catch (const std::exception&)
	{
		return ETextureImportResult::Failed;
	}
}

bool ExportTexture(const FExportConfig& Config, const FTexture& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	Archive::FMemoryWriter Writer;
	const_cast<FTexture&>(Resource).Serialize(Writer);
	OutBytes = Writer.TakeBytes();
	return true;
}

} // namespace detail

void FAssetsResource::Serialize(Archive::FArchive& Ar)
{
	std::uint32_t Magic = CassetMagic;
	std::uint32_t Version = this->Version;
	std::uint8_t TypeByte = static_cast<std::uint8_t>(Type);
	Ar << Magic << Version << TypeByte;

	std::uint32_t Count = static_cast<std::uint32_t>(Dependencies.size());
	Ar << Count;
	if (Ar.IsReading())
	{
		Dependencies.clear();
		Dependencies.reserve(Count);
	}
	for (std::uint32_t I = 0; I < Count; ++I)
	{
		std::string Path;
		if (!Ar.IsReading())
		{
			Path.assign(Dependencies[I]);
		}
		Ar << Path;
		if (Ar.IsReading())
		{
			Dependencies.emplace_back(std::move(Path));
		}
	}

	if (Ar.IsReading())
	{
		if (Magic != CassetMagic)
		{
			throw std::runtime_error("casset: bad magic");
		}
		if (Version > CassetVersion)
		{
			throw std::runtime_error("casset: unsupported version");
		}
		this->Version = Version;
		Type = static_cast<EAssetType>(TypeByte);
	}
}

namespace
{

template <typename T>
void SerializeVector(Archive::FArchive& Ar, std::vector<T>& V)
{
	std::uint32_t Count = static_cast<std::uint32_t>(V.size());
	Ar << Count;
	if (Ar.IsReading())
	{
		V.clear();
		V.reserve(Count);
	}
	for (std::uint32_t I = 0; I < Count; ++I)
	{
		T Elem{};
		if (!Ar.IsReading())
		{
			Elem = V[I];
		}
		Ar << Elem;
		if (Ar.IsReading())
		{
			V.push_back(std::move(Elem));
		}
	}
}

} // namespace

void FTexture::Serialize(Archive::FArchive& Ar)
{
	FAssetsResource::Serialize(Ar);
	std::uint32_t Dim = static_cast<std::uint32_t>(Dimension);
	std::uint32_t Fmt = static_cast<std::uint32_t>(PixelFormat);
	std::uint32_t W = Width, H = Height, D = Depth, L = ArrayLayers, M = MipCount;
	bool bInSRGB = bSRGB;
	Ar << Dim << Fmt << W << H << D << L << M << bInSRGB;
	std::uint32_t Count = static_cast<std::uint32_t>(Pixels.size());
	Ar << Count;
	if (Ar.IsReading())
	{
		Pixels.clear();
		Pixels.reserve(Count);
	}
	for (std::uint32_t I = 0; I < Count; ++I)
	{
		std::uint8_t Byte = 0;
		if (!Ar.IsReading())
		{
			Byte = Pixels[I];
		}
		Ar << Byte;
		if (Ar.IsReading())
		{
			Pixels.push_back(Byte);
		}
	}
	if (Ar.IsReading())
	{
		Dimension = static_cast<ETextureDimension>(Dim);
		PixelFormat = static_cast<ETexturePixelFormat>(Fmt);
		Width = W;
		Height = H;
		Depth = D;
		ArrayLayers = L;
		MipCount = M;
		bSRGB = bInSRGB;
	}
}

void FStaticMesh::Serialize(Archive::FArchive& Ar)
{
	FAssetsResource::Serialize(Ar);
	Ar << MaterialPath;
	SerializeVector(Ar, Positions);
	SerializeVector(Ar, Normals);
	SerializeVector(Ar, UVs);
	SerializeVector(Ar, Indices);
}

void FSkeleton::Serialize(Archive::FArchive& Ar)
{
	FAssetsResource::Serialize(Ar);
	std::uint32_t Count = static_cast<std::uint32_t>(Bones.size());
	Ar << Count;
	if (Ar.IsReading())
	{
		Bones.clear();
		Bones.reserve(Count);
	}
	for (std::uint32_t I = 0; I < Count; ++I)
	{
		std::string Name;
		std::int32_t ParentIndex = -1;
		if (!Ar.IsReading())
		{
			Name = Bones[I].Name;
			ParentIndex = Bones[I].ParentIndex;
		}
		Ar << Name << ParentIndex;
		float BindLocal[16] = { 0.f };
		if (!Ar.IsReading())
		{
			std::memcpy(BindLocal, Bones[I].BindLocal, sizeof(BindLocal));
		}
		for (int J = 0; J < 16; ++J)
		{
			Ar << BindLocal[J];
		}
		if (Ar.IsReading())
		{
			FSkeletonBone Bone;
			Bone.Name = std::move(Name);
			Bone.ParentIndex = ParentIndex;
			std::memcpy(Bone.BindLocal, BindLocal, sizeof(BindLocal));
			Bones.push_back(std::move(Bone));
		}
	}
}

void FAnimation::Serialize(Archive::FArchive& Ar)
{
	FAssetsResource::Serialize(Ar);
	Ar << SkeletonPath << DurationSeconds;
	std::uint32_t Count = static_cast<std::uint32_t>(Tracks.size());
	Ar << Count;
	if (Ar.IsReading())
	{
		Tracks.clear();
		Tracks.reserve(Count);
	}
	for (std::uint32_t I = 0; I < Count; ++I)
	{
		std::string TargetBoneName;
		if (!Ar.IsReading())
		{
			TargetBoneName = Tracks[I].TargetBoneName;
		}
		Ar << TargetBoneName;
		std::uint32_t KeyCount = 0;
		if (!Ar.IsReading())
		{
			KeyCount = static_cast<std::uint32_t>(Tracks[I].Keys.size());
		}
		Ar << KeyCount;
		std::vector<FAnimationKey> Keys;
		if (Ar.IsReading())
		{
			Keys.reserve(KeyCount);
		}
		for (std::uint32_t K = 0; K < KeyCount; ++K)
		{
			FAnimationKey Key{};
			if (!Ar.IsReading())
			{
				Key = Tracks[I].Keys[K];
			}
			Ar << Key.Time;
			for (int J = 0; J < 3; ++J) { Ar << Key.Translation[J]; }
			for (int J = 0; J < 4; ++J) { Ar << Key.Rotation[J]; }
			for (int J = 0; J < 3; ++J) { Ar << Key.Scale[J]; }
			if (Ar.IsReading())
			{
				Keys.push_back(std::move(Key));
			}
		}
		if (Ar.IsReading())
		{
			FAnimationTrack Track;
			Track.TargetBoneName = std::move(TargetBoneName);
			Track.Keys = std::move(Keys);
			Tracks.push_back(std::move(Track));
		}
	}
}

bool TResourceImporter<FTexture1D>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FTexture1D& Out, FResourceSystem& System)
{
	(void)System;
	FDecodedImage Img;
	switch (detail::ImportTexture(Bytes, Out, Img))
	{
	case detail::ETextureImportResult::Handled: return true;
	case detail::ETextureImportResult::Failed: return false;
	default: break;
	}
	if (!TextureImageCodec::DecodeFromMemory(Bytes.data(), Bytes.size(), Config.SourcePath, Img))
	{
		return false;
	}
	Out = FTexture1D(std::string(Out.GetPath()), Img.Format, Img.Width, Img.ArrayLayers, Img.MipCount, Img.bSRGB, std::move(Img.Pixels));
	return true;
}

bool TResourceExporter<FTexture1D>::Export(const FExportConfig& Config, const FTexture1D& Resource, std::vector<std::uint8_t>& OutBytes)
{
	return detail::ExportTexture(Config, Resource, OutBytes);
}

bool TResourceImporter<FTexture2D>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FTexture2D& Out, FResourceSystem& System)
{
	(void)System;
	FDecodedImage Img;
	switch (detail::ImportTexture(Bytes, Out, Img))
	{
	case detail::ETextureImportResult::Handled: return true;
	case detail::ETextureImportResult::Failed: return false;
	default: break;
	}
	if (!TextureImageCodec::DecodeFromMemory(Bytes.data(), Bytes.size(), Config.SourcePath, Img))
	{
		return false;
	}
	Out = FTexture2D(std::string(Out.GetPath()), Img.Format, Img.Width, Img.Height, Img.MipCount, Img.bSRGB, std::move(Img.Pixels));
	return true;
}

bool TResourceExporter<FTexture2D>::Export(const FExportConfig& Config, const FTexture2D& Resource, std::vector<std::uint8_t>& OutBytes)
{
	return detail::ExportTexture(Config, Resource, OutBytes);
}

bool TResourceImporter<FTexture3D>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FTexture3D& Out, FResourceSystem& System)
{
	(void)System;
	FDecodedImage Img;
	switch (detail::ImportTexture(Bytes, Out, Img))
	{
	case detail::ETextureImportResult::Handled: return true;
	case detail::ETextureImportResult::Failed: return false;
	default: break;
	}
	if (!TextureImageCodec::DecodeFromMemory(Bytes.data(), Bytes.size(), Config.SourcePath, Img))
	{
		return false;
	}
	Out = FTexture3D(std::string(Out.GetPath()), Img.Format, Img.Width, Img.Height, Img.Depth, Img.MipCount, Img.bSRGB, std::move(Img.Pixels));
	return true;
}

bool TResourceExporter<FTexture3D>::Export(const FExportConfig& Config, const FTexture3D& Resource, std::vector<std::uint8_t>& OutBytes)
{
	return detail::ExportTexture(Config, Resource, OutBytes);
}

bool TResourceImporter<FTextureCube>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FTextureCube& Out, FResourceSystem& System)
{
	(void)System;
	FDecodedImage Img;
	switch (detail::ImportTexture(Bytes, Out, Img))
	{
	case detail::ETextureImportResult::Handled: return true;
	case detail::ETextureImportResult::Failed: return false;
	default: break;
	}
	if (!TextureImageCodec::DecodeFromMemory(Bytes.data(), Bytes.size(), Config.SourcePath, Img))
	{
		return false;
	}
	Out = FTextureCube(std::string(Out.GetPath()), Img.Format, Img.Height, Img.MipCount, Img.bSRGB, std::move(Img.Pixels));
	return true;
}

bool TResourceExporter<FTextureCube>::Export(const FExportConfig& Config, const FTextureCube& Resource, std::vector<std::uint8_t>& OutBytes)
{
	return detail::ExportTexture(Config, Resource, OutBytes);
}

bool TResourceImporter<FTexture2DArray>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FTexture2DArray& Out, FResourceSystem& System)
{
	(void)System;
	FDecodedImage Img;
	switch (detail::ImportTexture(Bytes, Out, Img))
	{
	case detail::ETextureImportResult::Handled: return true;
	case detail::ETextureImportResult::Failed: return false;
	default: break;
	}
	if (!TextureImageCodec::DecodeFromMemory(Bytes.data(), Bytes.size(), Config.SourcePath, Img))
	{
		return false;
	}
	Out = FTexture2DArray(std::string(Out.GetPath()), Img.Format, Img.Width, Img.Height, Img.ArrayLayers, Img.MipCount, Img.bSRGB, std::move(Img.Pixels));
	return true;
}

bool TResourceExporter<FTexture2DArray>::Export(const FExportConfig& Config, const FTexture2DArray& Resource, std::vector<std::uint8_t>& OutBytes)
{
	return detail::ExportTexture(Config, Resource, OutBytes);
}

bool TResourceImporter<FTextureCubeArray>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FTextureCubeArray& Out, FResourceSystem& System)
{
	(void)System;
	FDecodedImage Img;
	switch (detail::ImportTexture(Bytes, Out, Img))
	{
	case detail::ETextureImportResult::Handled: return true;
	case detail::ETextureImportResult::Failed: return false;
	default: break;
	}
	if (!TextureImageCodec::DecodeFromMemory(Bytes.data(), Bytes.size(), Config.SourcePath, Img))
	{
		return false;
	}
	Out = FTextureCubeArray(std::string(Out.GetPath()), Img.Format, Img.Height, Img.ArrayLayers, Img.MipCount, Img.bSRGB, std::move(Img.Pixels));
	return true;
}

bool TResourceExporter<FTextureCubeArray>::Export(const FExportConfig& Config, const FTextureCubeArray& Resource, std::vector<std::uint8_t>& OutBytes)
{
	return detail::ExportTexture(Config, Resource, OutBytes);
}

// -- importer / exporter specializations ------------------------------------

bool TResourceImporter<FMaterial>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FMaterial& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	if (Bytes.empty())
	{
		return false;
	}
	Archive::FMemoryReader Reader(Bytes);
	try
	{
		Out.Serialize(Reader);
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}

bool TResourceExporter<FMaterial>::Export(const FExportConfig& Config, const FMaterial& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	Archive::FMemoryWriter Writer;
	const_cast<FMaterial&>(Resource).Serialize(Writer);
	OutBytes = Writer.TakeBytes();
	return true;
}

bool TResourceImporter<FStaticMesh>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FStaticMesh& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	if (Bytes.empty())
	{
		return false;
	}
	Archive::FMemoryReader Reader(Bytes);
	try
	{
		Out.Serialize(Reader);
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}

bool TResourceExporter<FStaticMesh>::Export(const FExportConfig& Config, const FStaticMesh& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	Archive::FMemoryWriter Writer;
	const_cast<FStaticMesh&>(Resource).Serialize(Writer);
	OutBytes = Writer.TakeBytes();
	return true;
}

bool TResourceImporter<FSkeleton>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FSkeleton& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	if (Bytes.empty())
	{
		return false;
	}
	Archive::FMemoryReader Reader(Bytes);
	try
	{
		Out.Serialize(Reader);
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}

bool TResourceExporter<FSkeleton>::Export(const FExportConfig& Config, const FSkeleton& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	Archive::FMemoryWriter Writer;
	const_cast<FSkeleton&>(Resource).Serialize(Writer);
	OutBytes = Writer.TakeBytes();
	return true;
}

bool TResourceImporter<FAnimation>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FAnimation& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	if (Bytes.empty())
	{
		return false;
	}
	Archive::FMemoryReader Reader(Bytes);
	try
	{
		Out.Serialize(Reader);
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}

bool TResourceExporter<FAnimation>::Export(const FExportConfig& Config, const FAnimation& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	Archive::FMemoryWriter Writer;
	const_cast<FAnimation&>(Resource).Serialize(Writer);
	OutBytes = Writer.TakeBytes();
	return true;
}

bool TResourceImporter<FAnimationGraph>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FAnimationGraph& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	if (Bytes.empty())
	{
		return false;
	}
	Archive::FMemoryReader Reader(Bytes);
	try
	{
		Out.Serialize(Reader);
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}

bool TResourceExporter<FAnimationGraph>::Export(const FExportConfig& Config, const FAnimationGraph& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	Archive::FMemoryWriter Writer;
	const_cast<FAnimationGraph&>(Resource).Serialize(Writer);
	OutBytes = Writer.TakeBytes();
	return true;
}

bool TResourceImporter<FPrefab>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FPrefab& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	if (Bytes.empty())
	{
		return false;
	}
	Archive::FMemoryReader Reader(Bytes);
	try
	{
		Out.Serialize(Reader);
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}

bool TResourceExporter<FPrefab>::Export(const FExportConfig& Config, const FPrefab& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	Archive::FMemoryWriter Writer;
	const_cast<FPrefab&>(Resource).Serialize(Writer);
	OutBytes = Writer.TakeBytes();
	return true;
}

} // namespace Resource
} // namespace Maho
