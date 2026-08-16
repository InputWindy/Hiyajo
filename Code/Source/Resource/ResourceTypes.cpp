#include "Resource/ResourceTypes.h"

namespace Maho
{

namespace
{
constexpr std::uint16_t kCpuLayoutVersion = 1;
}

// ── FTexture ───────────────────────────────────────────────────

FTexture::FTexture(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FResource(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::Texture2D : InType,
		std::move(InSourcePath))
{
}

void FTexture::SetCpuImage(
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
	Depth = InDepth == 0 ? 1 : InDepth;
	ArrayLayers = InArrayLayers == 0 ? 1 : InArrayLayers;
	MipCount = InMipCount == 0 ? 1 : InMipCount;
	bSRGB = bInSRGB;
	Pixels = std::move(InPixels);
	++ContentGeneration;
}

void FTexture::SetSerializedSource(std::string Hint, std::vector<std::uint8_t> Bytes)
{
	SerializedSourceHint = std::move(Hint);
	SerializedSourceBytes = std::move(Bytes);
}

void FTexture::ClearSerializedSource()
{
	SerializedSourceHint.clear();
	SerializedSourceBytes.clear();
}

// ── FTexture2D ─────────────────────────────────────────────────

FTexture2D::FTexture2D(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FTexture(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::Texture2D : InType,
		std::move(InSourcePath))
{
	Dimension = ETextureDimension::Tex2D;
}

// ── FTexture3D ─────────────────────────────────────────────────

FTexture3D::FTexture3D(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FTexture(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::Texture3D : InType,
		std::move(InSourcePath))
{
	Dimension = ETextureDimension::Tex3D;
}

// ── FTextureCube ───────────────────────────────────────────────

FTextureCube::FTextureCube(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FTexture(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::TextureCube : InType,
		std::move(InSourcePath))
{
	Dimension = ETextureDimension::Cube;
	ArrayLayers = 6;
}

// ── FTextureCubeArray ──────────────────────────────────────────

FTextureCubeArray::FTextureCubeArray(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FTexture(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::TextureCubeArray : InType,
		std::move(InSourcePath))
{
	Dimension = ETextureDimension::CubeArray;
}

// ── FTexture2DArray ────────────────────────────────────────────

FTexture2DArray::FTexture2DArray(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FTexture(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::Texture2DArray : InType,
		std::move(InSourcePath))
{
	Dimension = ETextureDimension::Tex2DArray;
}

// ── FMaterial ──────────────────────────────────────────────────

FMaterial::FMaterial(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FResource(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::Material : InType,
		std::move(InSourcePath))
{
}

std::vector<std::string> FMaterial::GetReferencePaths() const
{
	std::vector<std::string> Refs;
	if (!BaseColorPath.empty())
	{
		Refs.push_back(BaseColorPath);
	}
	if (!NormalPath.empty())
	{
		Refs.push_back(NormalPath);
	}
	if (!MetallicRoughnessPath.empty())
	{
		Refs.push_back(MetallicRoughnessPath);
	}
	if (!OcclusionPath.empty())
	{
		Refs.push_back(OcclusionPath);
	}
	if (!EmissivePath.empty())
	{
		Refs.push_back(EmissivePath);
	}
	return Refs;
}

// ── FStaticMesh ────────────────────────────────────────────────

FStaticMesh::FStaticMesh(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FResource(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::Mesh : InType,
		std::move(InSourcePath))
{
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
	++ContentGeneration;
}

std::vector<std::string> FStaticMesh::GetReferencePaths() const
{
	std::vector<std::string> Refs;
	if (!MaterialPath.empty())
	{
		Refs.push_back(MaterialPath);
	}
	return Refs;
}

// ── FSkeleton ──────────────────────────────────────────────────

FSkeleton::FSkeleton(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FResource(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::Skeleton : InType,
		std::move(InSourcePath))
{
}

void FSkeleton::SetBones(std::vector<FSkeletonBone> InBones)
{
	Bones = std::move(InBones);
	++ContentGeneration;
}

// ── FAnimation ─────────────────────────────────────────────────

FAnimation::FAnimation(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FResource(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::Animation : InType,
		std::move(InSourcePath))
{
}

void FAnimation::SetSkeleton(std::string Path)
{
	SkeletonPath = std::move(Path);
	++ContentGeneration;
}

void FAnimation::SetDurationSeconds(float Seconds)
{
	DurationSeconds = Seconds;
	++ContentGeneration;
}

void FAnimation::SetTracks(std::vector<FAnimationTrack> InTracks)
{
	Tracks = std::move(InTracks);
	++ContentGeneration;
}

std::vector<std::string> FAnimation::GetReferencePaths() const
{
	std::vector<std::string> Refs;
	if (!SkeletonPath.empty())
	{
		Refs.push_back(SkeletonPath);
	}
	return Refs;
}

// ── FAnimationGraph ────────────────────────────────────────────

FAnimationGraph::FAnimationGraph(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FResource(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::AnimationGraph : InType,
		std::move(InSourcePath))
{
}

// ── FPrefab ────────────────────────────────────────────────────

FPrefab::FPrefab(
	std::string InName,
	EAssetType InType,
	std::string InSourcePath)
	: FResource(
		std::move(InName),
		InType == EAssetType::Unknown ? EAssetType::Prefab : InType,
		std::move(InSourcePath))
{
}

// ── Serialize ───────────────────────────────────────────────────

void FTexture::Serialize(FArchive& Ar)
{
	FResource::Serialize(Ar);

	std::uint16_t Version = 1;
	Ar << Version;

	bool bEncoded = Ar.IsSaving() ? HasSerializedSource() : false;
	std::uint8_t EncodeByte = bEncoded ? 1 : 0;
	Ar << EncodeByte;
	if (Ar.IsLoading())
	{
		bEncoded = (EncodeByte != 0);
	}

	if (bEncoded)
	{
		std::uint8_t Dim = static_cast<std::uint8_t>(Dimension);
		Ar << Dim;
		if (Ar.IsLoading())
		{
			Dimension = static_cast<ETextureDimension>(Dim);
		}

		Ar.SerializeString(SerializedSourceHint);

		std::uint32_t Count = Ar.IsSaving() ? static_cast<std::uint32_t>(SerializedSourceBytes.size()) : 0;
		Ar << Count;
		if (Ar.IsLoading())
		{
			SerializedSourceBytes.resize(Count);
		}
		Ar.SerializeBytes(SerializedSourceBytes.data(), Count);
		return;
	}

	std::uint8_t Dim = static_cast<std::uint8_t>(Dimension);
	Ar << Dim;
	if (Ar.IsLoading())
	{
		Dimension = static_cast<ETextureDimension>(Dim);
	}

	std::uint32_t Fmt = static_cast<std::uint32_t>(PixelFormat);
	Ar << Fmt;
	if (Ar.IsLoading())
	{
		PixelFormat = static_cast<ETexturePixelFormat>(Fmt);
	}

	Ar << Width << Height << Depth << ArrayLayers << MipCount;

	std::uint8_t SRGB = bSRGB ? 1 : 0;
	Ar << SRGB;
	if (Ar.IsLoading())
	{
		bSRGB = (SRGB != 0);
	}

	std::uint32_t PixelCount = Ar.IsSaving() ? static_cast<std::uint32_t>(Pixels.size()) : 0;
	Ar << PixelCount;
	if (Ar.IsLoading())
	{
		Pixels.resize(PixelCount);
	}
	Ar.SerializeBytes(Pixels.data(), PixelCount);
}

void FMaterial::Serialize(FArchive& Ar)
{
	FResource::Serialize(Ar);

	std::uint16_t Version = kCpuLayoutVersion;
	Ar << Version;

	Ar.SerializeString(BaseColorPath);
	Ar.SerializeString(NormalPath);
	Ar.SerializeString(MetallicRoughnessPath);
	Ar.SerializeString(OcclusionPath);
	Ar.SerializeString(EmissivePath);
	for (int I = 0; I < 4; ++I)
	{
		Ar << BaseColorFactor[I];
	}
	Ar << MetallicFactor;
	Ar << RoughnessFactor;
	for (int I = 0; I < 3; ++I)
	{
		Ar << EmissiveFactor[I];
	}
}

void FStaticMesh::Serialize(FArchive& Ar)
{
	FResource::Serialize(Ar);

	std::uint16_t Version = kCpuLayoutVersion;
	Ar << Version;
	Ar.SerializeString(MaterialPath);

	std::uint32_t PosCount = Ar.IsSaving() ? static_cast<std::uint32_t>(Positions.size()) : 0;
	Ar << PosCount;
	if (Ar.IsLoading())
	{
		Positions.resize(PosCount);
	}
	Ar.SerializeBytes(Positions.data(), PosCount * sizeof(float));

	std::uint32_t NormCount = Ar.IsSaving() ? static_cast<std::uint32_t>(Normals.size()) : 0;
	Ar << NormCount;
	if (Ar.IsLoading())
	{
		Normals.resize(NormCount);
	}
	Ar.SerializeBytes(Normals.data(), NormCount * sizeof(float));

	std::uint32_t UvCount = Ar.IsSaving() ? static_cast<std::uint32_t>(UVs.size()) : 0;
	Ar << UvCount;
	if (Ar.IsLoading())
	{
		UVs.resize(UvCount);
	}
	Ar.SerializeBytes(UVs.data(), UvCount * sizeof(float));

	std::uint32_t IdxCount = Ar.IsSaving() ? static_cast<std::uint32_t>(Indices.size()) : 0;
	Ar << IdxCount;
	if (Ar.IsLoading())
	{
		Indices.resize(IdxCount);
	}
	Ar.SerializeBytes(Indices.data(), IdxCount * sizeof(std::uint32_t));
}

void FSkeleton::Serialize(FArchive& Ar)
{
	FResource::Serialize(Ar);

	std::uint16_t Version = kCpuLayoutVersion;
	Ar << Version;

	std::uint32_t BoneCount = Ar.IsSaving() ? static_cast<std::uint32_t>(Bones.size()) : 0;
	Ar << BoneCount;
	if (Ar.IsLoading())
	{
		Bones.resize(BoneCount);
	}

	for (std::uint32_t I = 0; I < BoneCount; ++I)
	{
		Ar.SerializeString(Bones[I].Name);
		Ar << Bones[I].ParentIndex;
		Ar.SerializeBytes(Bones[I].BindLocal, 16 * sizeof(float));
	}
}

void FAnimation::Serialize(FArchive& Ar)
{
	FResource::Serialize(Ar);

	std::uint16_t Version = kCpuLayoutVersion;
	Ar << Version;
	Ar.SerializeString(SkeletonPath);
	Ar << DurationSeconds;

	std::uint32_t TrackCount = Ar.IsSaving() ? static_cast<std::uint32_t>(Tracks.size()) : 0;
	Ar << TrackCount;
	if (Ar.IsLoading())
	{
		Tracks.resize(TrackCount);
	}

	for (std::uint32_t I = 0; I < TrackCount; ++I)
	{
		Ar.SerializeString(Tracks[I].TargetBoneName);
		std::uint32_t KeyCount = Ar.IsSaving() ? static_cast<std::uint32_t>(Tracks[I].Keys.size()) : 0;
		Ar << KeyCount;
		if (Ar.IsLoading())
		{
			Tracks[I].Keys.resize(KeyCount);
		}

		for (std::uint32_t J = 0; J < KeyCount; ++J)
		{
			Ar << Tracks[I].Keys[J].Time;
			Ar.SerializeBytes(Tracks[I].Keys[J].Translation, 3 * sizeof(float));
			Ar.SerializeBytes(Tracks[I].Keys[J].Rotation, 4 * sizeof(float));
			Ar.SerializeBytes(Tracks[I].Keys[J].Scale, 3 * sizeof(float));
		}
	}
}

void FAnimationGraph::Serialize(FArchive& Ar)
{
	FResource::Serialize(Ar);

	std::uint16_t Version = kCpuLayoutVersion;
	Ar << Version;
	Ar.SerializeString(DocumentJson);
}

void FPrefab::Serialize(FArchive& Ar)
{
	FResource::Serialize(Ar);

	std::uint16_t Version = kCpuLayoutVersion;
	Ar << Version;
	Ar.SerializeString(DocumentJson);
}

} // namespace Maho
