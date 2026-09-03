#include "ResourceTypes.h"

#include "TextureImageCodec.h"
#include "ResourceBinary.h"

#include <array>
#include <cstring>
#include <utility>

namespace Maho
{
namespace Resource
{

using namespace detail;

// -- FTexture2D helpers -----------------------------------------------------

namespace
{

// Snapshot magic (8 bytes) marking a FTexture2D binary export. Lets the importer
// round-trip our own exported snapshot; otherwise it decodes a standard image.
constexpr std::array<std::uint8_t, 8> kSnapMagic = { 'M', 'H', 'T', 'E', 'X', '1', 0, 42 };

bool HasSnapMagic(std::span<const std::uint8_t> Bytes)
{
	return Bytes.size() >= kSnapMagic.size()
		&& std::memcmp(Bytes.data(), kSnapMagic.data(), kSnapMagic.size()) == 0;
}

void WriteSnap(FBinWriter& W, const FTexture2D& R)
{
	W.WriteU32(static_cast<std::uint32_t>(R.GetDimension()));
	W.WriteU32(static_cast<std::uint32_t>(R.GetPixelFormat()));
	W.WriteU32(R.GetWidth());
	W.WriteU32(R.GetHeight());
	W.WriteU32(R.GetDepth());
	W.WriteU32(R.GetArrayLayers());
	W.WriteU32(R.GetMipCount());
	W.WriteBool(R.IsSRGB());
	const auto& Pixels = R.GetPixels();
	W.WriteU32(static_cast<std::uint32_t>(Pixels.size()));
	for (const std::uint8_t Byte : Pixels)
	{
		W.WriteU32(Byte);
	}
}

	bool ReadSnap(FBinReader& R, FTexture2D& Out)
	{
		// The 8 magic bytes were already consumed by the caller's subspan.
		std::uint32_t Dimension = 0;
		std::uint32_t Format = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t Depth = 0;
		std::uint32_t ArrayLayers = 0;
		std::uint32_t MipCount = 0;
		bool bSRGB = false;
	if (!R.ReadU32(Dimension) || !R.ReadU32(Format) || !R.ReadU32(Width)
		|| !R.ReadU32(Height) || !R.ReadU32(Depth) || !R.ReadU32(ArrayLayers)
		|| !R.ReadU32(MipCount) || !R.ReadBool(bSRGB))
	{
		return false;
	}
	std::uint32_t PixelCount = 0;
	if (!R.ReadU32(PixelCount))
	{
		return false;
	}
	std::vector<std::uint8_t> Pixels;
	Pixels.reserve(PixelCount);
	for (std::uint32_t I = 0; I < PixelCount; ++I)
	{
		std::uint32_t Byte = 0;
		if (!R.ReadU32(Byte))
		{
			return false;
		}
		Pixels.push_back(static_cast<std::uint8_t>(Byte));
	}
	Out.SetCpuImage(
		static_cast<ETextureDimension>(Dimension),
		static_cast<ETexturePixelFormat>(Format),
		Width,
		Height,
		Depth,
		ArrayLayers,
		MipCount,
		bSRGB,
		std::move(Pixels));
	return true;
}

} // namespace

void FTexture2D::SetCpuImage(
	ETextureDimension InDimension,
	ETexturePixelFormat InFormat,
	std::uint32_t InWidth,
	std::uint32_t InHeight,
	std::uint32_t InDepth,
	std::uint32_t InArrayLayers,
	std::uint32_t InMipCount,
	bool bInSRGB,
	std::vector<std::uint8_t> InPixels)
{
	Dimension = InDimension;
	PixelFormat = InFormat;
	Width = InWidth;
	Height = InHeight;
	Depth = InDepth;
	ArrayLayers = InArrayLayers;
	MipCount = InMipCount;
	bSRGB = bInSRGB;
	Pixels = std::move(InPixels);
}

void FStaticMesh::SetCpuGeometry(
	std::vector<float> InPositions,
	std::vector<float> InNormals,
	std::vector<float> InUVs,
	std::vector<std::uint32_t> InIndices)
{
	Positions = std::move(InPositions);
	Normals = std::move(InNormals);
	UVs = std::move(InUVs);
	Indices = std::move(InIndices);
}

bool TResourceImporter<FTexture2D>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FTexture2D& Out, FResourceSystem& System)
{
	(void)System;
	if (Bytes.empty())
	{
		return false;
	}

	// Round-trip our own binary snapshot first, else decode a standard image.
	if (HasSnapMagic(Bytes))
	{
		FBinReader Reader(Bytes.subspan(kSnapMagic.size()));
		return ReadSnap(Reader, Out);
	}

	FDecodedImage Img;
	if (!TextureImageCodec::DecodeFromMemory(Bytes.data(), Bytes.size(), Config.SourcePath, Img))
	{
		return false;
	}
	Out.SetCpuImage(
		Img.Dimension,
		Img.Format,
		Img.Width,
		Img.Height,
		Img.Depth,
		Img.ArrayLayers,
		Img.MipCount,
		Img.bSRGB,
		std::move(Img.Pixels));
	return true;
}

bool TResourceExporter<FTexture2D>::Export(const FExportConfig& Config, const FTexture2D& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	OutBytes.clear();
	FBinWriter Writer(OutBytes);
	for (const std::uint8_t Byte : kSnapMagic)
	{
		OutBytes.push_back(Byte);
	}
	WriteSnap(Writer, Resource);
	return true;
}

// -- value-type binary codec (mirror each type's fields) --------------------

namespace
{

void EncodeF32Array(FBinWriter& W, const float* V, int N)
{
	for (int I = 0; I < N; ++I)
	{
		W.WriteF32(V[I]);
	}
}

bool ReadF32Array(FBinReader& R, float* V, int N)
{
	for (int I = 0; I < N; ++I)
	{
		if (!R.ReadF32(V[I]))
		{
			return false;
		}
	}
	return true;
}

template <typename T, typename EncodeFn>
void EncodeVec(FBinWriter& W, const std::vector<T>& V, EncodeFn Encode)
{
	W.WriteU32(static_cast<std::uint32_t>(V.size()));
	for (const T& Elem : V)
	{
		Encode(W, Elem);
	}
}

template <typename T, typename DecodeFn>
bool DecodeVec(FBinReader& R, std::vector<T>& V, DecodeFn Decode)
{
	std::uint32_t Count = 0;
	if (!R.ReadU32(Count))
	{
		return false;
	}
	V.clear();
	V.reserve(Count);
	for (std::uint32_t I = 0; I < Count; ++I)
	{
		T Elem{};
		if (!Decode(R, Elem))
		{
			return false;
		}
		V.push_back(std::move(Elem));
	}
	return true;
}

void EncodeAnimKey(FBinWriter& W, const FAnimationKey& K)
{
	W.WriteF32(K.Time);
	EncodeF32Array(W, K.Translation, 3);
	EncodeF32Array(W, K.Rotation, 4);
	EncodeF32Array(W, K.Scale, 3);
}

bool DecodeAnimKey(FBinReader& R, FAnimationKey& K)
{
	return R.ReadF32(K.Time)
		&& ReadF32Array(R, K.Translation, 3)
		&& ReadF32Array(R, K.Rotation, 4)
		&& ReadF32Array(R, K.Scale, 3);
}

void EncodeBone(FBinWriter& W, const FSkeletonBone& B)
{
	W.WriteString(B.Name);
	W.WriteI32(B.ParentIndex);
	EncodeF32Array(W, B.BindLocal, 16);
}

bool DecodeBone(FBinReader& R, FSkeletonBone& B)
{
	return R.ReadString(B.Name)
		&& R.ReadI32(B.ParentIndex)
		&& ReadF32Array(R, B.BindLocal, 16);
}

void EncodeTrack(FBinWriter& W, const FAnimationTrack& T)
{
	W.WriteString(T.TargetBoneName);
	EncodeVec(W, T.Keys, EncodeAnimKey);
}

bool DecodeTrack(FBinReader& R, FAnimationTrack& T)
{
	return R.ReadString(T.TargetBoneName)
		&& DecodeVec(R, T.Keys, DecodeAnimKey);
}

void EncodeMaterial(FBinWriter& W, const FMaterial& M)
{
	EncodeF32Array(W, M.BaseColorFactor, 4);
	W.WriteF32(M.MetallicFactor);
	W.WriteF32(M.RoughnessFactor);
	EncodeF32Array(W, M.EmissiveFactor, 3);
	W.WriteString(M.GetBaseColorTexture());
	W.WriteString(M.GetNormalTexture());
	W.WriteString(M.GetMetallicRoughnessTexture());
	W.WriteString(M.GetOcclusionTexture());
	W.WriteString(M.GetEmissiveTexture());
}

bool DecodeMaterial(FBinReader& R, FMaterial& M)
{
	if (!ReadF32Array(R, M.BaseColorFactor, 4)
		|| !R.ReadF32(M.MetallicFactor)
		|| !R.ReadF32(M.RoughnessFactor)
		|| !ReadF32Array(R, M.EmissiveFactor, 3))
	{
		return false;
	}
	std::string BaseColor;
	std::string Normal;
	std::string MetallicRoughness;
	std::string Occlusion;
	std::string Emissive;
	if (!R.ReadString(BaseColor) || !R.ReadString(Normal) || !R.ReadString(MetallicRoughness)
		|| !R.ReadString(Occlusion) || !R.ReadString(Emissive))
	{
		return false;
	}
	M.SetBaseColorTexture(std::move(BaseColor));
	M.SetNormalTexture(std::move(Normal));
	M.SetMetallicRoughnessTexture(std::move(MetallicRoughness));
	M.SetOcclusionTexture(std::move(Occlusion));
	M.SetEmissiveTexture(std::move(Emissive));
	return true;
}

void EncodeStaticMesh(FBinWriter& W, const FStaticMesh& M)
{
	W.WriteString(M.GetMaterial());
	EncodeVec(W, M.GetPositions(), [](FBinWriter& W2, float V) { W2.WriteF32(V); });
	EncodeVec(W, M.GetNormals(), [](FBinWriter& W2, float V) { W2.WriteF32(V); });
	EncodeVec(W, M.GetUVs(), [](FBinWriter& W2, float V) { W2.WriteF32(V); });
	EncodeVec(W, M.GetIndices(), [](FBinWriter& W2, std::uint32_t V) { W2.WriteU32(V); });
}

bool DecodeStaticMesh(FBinReader& R, FStaticMesh& M)
{
	std::string MaterialPath;
	if (!R.ReadString(MaterialPath))
	{
		return false;
	}
	std::vector<float> Positions;
	std::vector<float> Normals;
	std::vector<float> UVs;
	std::vector<std::uint32_t> Indices;
	if (!DecodeVec(R, Positions, [](FBinReader& R2, float& V) { return R2.ReadF32(V); })
		|| !DecodeVec(R, Normals, [](FBinReader& R2, float& V) { return R2.ReadF32(V); })
		|| !DecodeVec(R, UVs, [](FBinReader& R2, float& V) { return R2.ReadF32(V); })
		|| !DecodeVec(R, Indices, [](FBinReader& R2, std::uint32_t& V) { return R2.ReadU32(V); }))
	{
		return false;
	}
	M.SetMaterial(std::move(MaterialPath));
	M.SetCpuGeometry(std::move(Positions), std::move(Normals), std::move(UVs), std::move(Indices));
	return true;
}

void EncodeSkeleton(FBinWriter& W, const FSkeleton& S)
{
	EncodeVec(W, S.GetBones(), EncodeBone);
}

bool DecodeSkeleton(FBinReader& R, FSkeleton& S)
{
	std::vector<FSkeletonBone> Bones;
	if (!DecodeVec(R, Bones, DecodeBone))
	{
		return false;
	}
	S.SetBones(std::move(Bones));
	return true;
}

void EncodeAnimation(FBinWriter& W, const FAnimation& A)
{
	W.WriteString(A.GetSkeleton());
	W.WriteF32(A.GetDurationSeconds());
	EncodeVec(W, A.GetTracks(), EncodeTrack);
}

bool DecodeAnimation(FBinReader& R, FAnimation& A)
{
	std::string SkeletonPath;
	if (!R.ReadString(SkeletonPath))
	{
		return false;
	}
	float Duration = 0.f;
	if (!R.ReadF32(Duration))
	{
		return false;
	}
	std::vector<FAnimationTrack> Tracks;
	if (!DecodeVec(R, Tracks, DecodeTrack))
	{
		return false;
	}
	A.SetSkeleton(std::move(SkeletonPath));
	A.SetDurationSeconds(Duration);
	A.SetTracks(std::move(Tracks));
	return true;
}

void EncodeGraph(FBinWriter& W, const std::string& Json)
{
	W.WriteString(Json);
}

bool DecodeGraph(FBinReader& R, std::string& Json)
{
	return R.ReadString(Json);
}

} // namespace

// -- importer / exporter specializations ------------------------------------

bool TResourceImporter<FMaterial>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FMaterial& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	FBinReader Reader(Bytes);
	return DecodeMaterial(Reader, Out);
}

bool TResourceExporter<FMaterial>::Export(const FExportConfig& Config, const FMaterial& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	OutBytes.clear();
	FBinWriter Writer(OutBytes);
	EncodeMaterial(Writer, Resource);
	return true;
}

bool TResourceImporter<FStaticMesh>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FStaticMesh& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	FBinReader Reader(Bytes);
	return DecodeStaticMesh(Reader, Out);
}

bool TResourceExporter<FStaticMesh>::Export(const FExportConfig& Config, const FStaticMesh& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	OutBytes.clear();
	FBinWriter Writer(OutBytes);
	EncodeStaticMesh(Writer, Resource);
	return true;
}

bool TResourceImporter<FSkeleton>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FSkeleton& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	FBinReader Reader(Bytes);
	return DecodeSkeleton(Reader, Out);
}

bool TResourceExporter<FSkeleton>::Export(const FExportConfig& Config, const FSkeleton& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	OutBytes.clear();
	FBinWriter Writer(OutBytes);
	EncodeSkeleton(Writer, Resource);
	return true;
}

bool TResourceImporter<FAnimation>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FAnimation& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	FBinReader Reader(Bytes);
	return DecodeAnimation(Reader, Out);
}

bool TResourceExporter<FAnimation>::Export(const FExportConfig& Config, const FAnimation& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	OutBytes.clear();
	FBinWriter Writer(OutBytes);
	EncodeAnimation(Writer, Resource);
	return true;
}

bool TResourceImporter<FAnimationGraph>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FAnimationGraph& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	std::string Json;
	FBinReader Reader(Bytes);
	if (!DecodeGraph(Reader, Json))
	{
		return false;
	}
	Out.SetDocumentJson(std::move(Json));
	return true;
}

bool TResourceExporter<FAnimationGraph>::Export(const FExportConfig& Config, const FAnimationGraph& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	OutBytes.clear();
	FBinWriter Writer(OutBytes);
	EncodeGraph(Writer, Resource.GetDocumentJson());
	return true;
}

bool TResourceImporter<FPrefab>::Import(const FImportConfig& Config, std::span<const std::uint8_t> Bytes, FPrefab& Out, FResourceSystem& System)
{
	(void)Config;
	(void)System;
	std::string Json;
	FBinReader Reader(Bytes);
	if (!DecodeGraph(Reader, Json))
	{
		return false;
	}
	Out.SetDocumentJson(std::move(Json));
	return true;
}

bool TResourceExporter<FPrefab>::Export(const FExportConfig& Config, const FPrefab& Resource, std::vector<std::uint8_t>& OutBytes)
{
	(void)Config;
	OutBytes.clear();
	FBinWriter Writer(OutBytes);
	EncodeGraph(Writer, Resource.GetDocumentJson());
	return true;
}

} // namespace Resource
} // namespace Maho
