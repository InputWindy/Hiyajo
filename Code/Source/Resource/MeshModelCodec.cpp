#include "MeshModelCodec.h"
#include "TextureImageCodec.h"

#include <Core/Misc/Log.h>
#include <Core/Misc/Utf8Path.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

#if defined(MAHO_WITH_ASSIMP) && MAHO_WITH_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#endif

namespace Maho
{
namespace MeshModelCodec
{
namespace
{

std::string SanitizeObjectName(std::string_view Raw, std::string_view Fallback)
{
	std::string Out;
	Out.reserve(Raw.size());
	for (const unsigned char Ch : Raw)
	{
		// Keep UTF-8 multibyte (CJK) intact; map ASCII separators / punctuation to '_'.
		if (Ch >= 0x80)
		{
			Out.push_back(static_cast<char>(Ch));
			continue;
		}
		if (std::isalnum(Ch) || Ch == '_' || Ch == '-')
		{
			Out.push_back(static_cast<char>(Ch));
		}
		else
		{
			// '!', spaces, dots, path separators, etc. map to underscore (MMD often uses "Muse2!").
			Out.push_back('_');
		}
	}
	while (!Out.empty() && Out.back() == '_')
	{
		Out.pop_back();
	}
	if (Out.empty())
	{
		Out.assign(Fallback.begin(), Fallback.end());
	}
	return Out;
}

/** Package object names must be unique; MMD materials/meshes often share display names. */
[[nodiscard]] std::string MakeUniqueObjectName(
	FResourceSystem& Resources,
	const std::string& PackagePath,
	std::string BaseName)
{
	const std::string Key = Resources.MakeAssetCatalogKey(PackagePath, BaseName);
	if (!Resources.Find<FResource>(Key))
	{
		return BaseName;
	}
	for (int Suffix = 1; Suffix < 100000; ++Suffix)
	{
		const std::string Candidate = BaseName + "_" + std::to_string(Suffix);
		const std::string CandidateKey = Resources.MakeAssetCatalogKey(PackagePath, Candidate);
		if (!Resources.Find<FResource>(CandidateKey))
		{
			return Candidate;
		}
	}
	return BaseName + "_x";
}

void CopyMat4(float Out[16], const float* In)
{
	for (int I = 0; I < 16; ++I)
	{
		Out[I] = In[I];
	}
}

template <typename TResource>
[[nodiscard]] TResource* CreateRegisteredResource(
	FResourceSystem& Resources,
	const std::string& PackagePath,
	const std::string& DesiredName,
	EAssetType Type,
	const std::string& SourcePath)
{
	const std::string Name = MakeUniqueObjectName(Resources, PackagePath, DesiredName);
	if (Name != DesiredName)
	{
		MAHO_CORE_WARN(
			"MeshModelCodec: renamed '{}' -> '{}' in package '{}'",
			DesiredName,
			Name,
			PackagePath);
	}

	auto Resource = std::make_unique<TResource>(Name, Type, SourcePath);
	TResource* Raw = Resource.get();
	if (!Resources.RegisterResource(TRef<FResource>(Resource.release()), PackagePath))
		return nullptr;
	return Raw;
}

#if defined(MAHO_WITH_ASSIMP) && MAHO_WITH_ASSIMP

void CopyAiMatrix(float Out[16], const aiMatrix4x4& M)
{
	Out[0] = M.a1;
	Out[1] = M.b1;
	Out[2] = M.c1;
	Out[3] = M.d1;
	Out[4] = M.a2;
	Out[5] = M.b2;
	Out[6] = M.c2;
	Out[7] = M.d2;
	Out[8] = M.a3;
	Out[9] = M.b3;
	Out[10] = M.c3;
	Out[11] = M.d3;
	Out[12] = M.a4;
	Out[13] = M.b4;
	Out[14] = M.c4;
	Out[15] = M.d4;
}

void CollectNodes(
	const aiNode* Node,
	std::int32_t ParentIndex,
	std::vector<FDecodedSceneNode>& OutNodes)
{
	if (!Node)
	{
		return;
	}

	const std::int32_t SelfIndex = static_cast<std::int32_t>(OutNodes.size());
	FDecodedSceneNode Decoded;
	Decoded.Name = Node->mName.C_Str();
	Decoded.ParentIndex = ParentIndex;
	CopyAiMatrix(Decoded.LocalTransform, Node->mTransformation);
	Decoded.MeshIndex = Node->mNumMeshes > 0 ? static_cast<std::int32_t>(Node->mMeshes[0]) : -1;
	OutNodes.push_back(std::move(Decoded));

	for (unsigned I = 0; I < Node->mNumChildren; ++I)
	{
		CollectNodes(Node->mChildren[I], SelfIndex, OutNodes);
	}
}

void DecodeMesh(const aiMesh* Mesh, FDecodedMesh& Out)
{
	Out.Name = Mesh->mName.C_Str();
	Out.MaterialIndex = static_cast<std::int32_t>(Mesh->mMaterialIndex);
	Out.Positions.reserve(Mesh->mNumVertices * 3);
	Out.Normals.reserve(Mesh->mNumVertices * 3);
	Out.UVs.reserve(Mesh->mNumVertices * 2);

	for (unsigned V = 0; V < Mesh->mNumVertices; ++V)
	{
		Out.Positions.push_back(Mesh->mVertices[V].x);
		Out.Positions.push_back(Mesh->mVertices[V].y);
		Out.Positions.push_back(Mesh->mVertices[V].z);
		if (Mesh->HasNormals())
		{
			Out.Normals.push_back(Mesh->mNormals[V].x);
			Out.Normals.push_back(Mesh->mNormals[V].y);
			Out.Normals.push_back(Mesh->mNormals[V].z);
		}
		if (Mesh->HasTextureCoords(0))
		{
			Out.UVs.push_back(Mesh->mTextureCoords[0][V].x);
			Out.UVs.push_back(Mesh->mTextureCoords[0][V].y);
		}
		else
		{
			Out.UVs.push_back(0.f);
			Out.UVs.push_back(0.f);
		}
	}

	Out.Indices.reserve(Mesh->mNumFaces * 3);
	for (unsigned F = 0; F < Mesh->mNumFaces; ++F)
	{
		const aiFace& Face = Mesh->mFaces[F];
		if (Face.mNumIndices != 3)
		{
			continue;
		}
		Out.Indices.push_back(Face.mIndices[0]);
		Out.Indices.push_back(Face.mIndices[1]);
		Out.Indices.push_back(Face.mIndices[2]);
	}

	if (Mesh->mNumBones > 0)
	{
		Out.BoneWeights.resize(Mesh->mNumVertices);
		for (unsigned B = 0; B < Mesh->mNumBones; ++B)
		{
			const aiBone* Bone = Mesh->mBones[B];
			for (unsigned W = 0; W < Bone->mNumWeights; ++W)
			{
				const aiVertexWeight& VW = Bone->mWeights[W];
				if (VW.mVertexId >= Mesh->mNumVertices)
				{
					continue;
				}
				FDecodedBoneWeight Entry;
				Entry.BoneIndex = B;
				Entry.Weight = VW.mWeight;
				Out.BoneWeights[VW.mVertexId].push_back(Entry);
			}
		}
	}
}

void DecodeMaterials(const aiScene* Scene, FDecodedModelScene& Out)
{
	Out.Materials.reserve(Scene->mNumMaterials);
	for (unsigned I = 0; I < Scene->mNumMaterials; ++I)
	{
		const aiMaterial* Mat = Scene->mMaterials[I];
		FDecodedMaterial Decoded;
		aiString Name;
		if (Mat->Get(AI_MATKEY_NAME, Name) == AI_SUCCESS)
		{
			Decoded.Name = Name.C_Str();
		}
		else
		{
			Decoded.Name = "Material_" + std::to_string(I);
		}

		aiColor4D Color;
		if (Mat->Get(AI_MATKEY_BASE_COLOR, Color) == AI_SUCCESS
			|| Mat->Get(AI_MATKEY_COLOR_DIFFUSE, Color) == AI_SUCCESS)
		{
			Decoded.BaseColorFactor[0] = Color.r;
			Decoded.BaseColorFactor[1] = Color.g;
			Decoded.BaseColorFactor[2] = Color.b;
			Decoded.BaseColorFactor[3] = Color.a;
		}

		float Metallic = 0.f;
		float Roughness = 1.f;
		if (Mat->Get(AI_MATKEY_METALLIC_FACTOR, Metallic) == AI_SUCCESS)
		{
			Decoded.MetallicFactor = Metallic;
		}
		if (Mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, Roughness) == AI_SUCCESS)
		{
			Decoded.RoughnessFactor = Roughness;
		}

		aiColor3D Emissive;
		if (Mat->Get(AI_MATKEY_COLOR_EMISSIVE, Emissive) == AI_SUCCESS)
		{
			Decoded.EmissiveFactor[0] = Emissive.r;
			Decoded.EmissiveFactor[1] = Emissive.g;
			Decoded.EmissiveFactor[2] = Emissive.b;
		}

		auto HasSlot = [&](const char* Slot) -> bool
		{
			for (const FDecodedTextureRef& Tex : Decoded.Textures)
			{
				if (Tex.SlotName == Slot)
				{
					return true;
				}
			}
			return false;
		};

		auto TryAddTex = [&](aiTextureType Type, const char* Slot)
		{
			if (HasSlot(Slot))
			{
				return;
			}
			aiString TexPath;
			if (Mat->GetTexture(Type, 0, &TexPath) != AI_SUCCESS || TexPath.length == 0)
			{
				return;
			}
			FDecodedTextureRef Tex;
			Tex.SlotName = Slot;
			Tex.SourcePath = TexPath.C_Str();
			// Assimp embedded texture: "*0", "*1", ...
			if (!Tex.SourcePath.empty() && Tex.SourcePath[0] == '*')
			{
				const aiTexture* Embedded = Scene->GetEmbeddedTexture(TexPath.C_Str());
				if (Embedded && Embedded->mHeight == 0 && Embedded->pcData && Embedded->mWidth > 0)
				{
					const auto* Bytes = reinterpret_cast<const std::uint8_t*>(Embedded->pcData);
					Tex.EmbeddedBytes.assign(Bytes, Bytes + Embedded->mWidth);
				}
			}
			Decoded.Textures.push_back(std::move(Tex));
		};

		// glTF PBR first, then classic / fallbacks.
		TryAddTex(aiTextureType_BASE_COLOR, "BaseColor");
		TryAddTex(aiTextureType_DIFFUSE, "BaseColor");
		TryAddTex(aiTextureType_NORMAL_CAMERA, "Normal");
		TryAddTex(aiTextureType_NORMALS, "Normal");
		TryAddTex(aiTextureType_HEIGHT, "Normal");
		// glTF metallicRoughnessTexture often lands on UNKNOWN; metal/rough may also be split.
		TryAddTex(aiTextureType_UNKNOWN, "MetallicRoughness");
		TryAddTex(aiTextureType_METALNESS, "MetallicRoughness");
		TryAddTex(aiTextureType_DIFFUSE_ROUGHNESS, "MetallicRoughness");
		TryAddTex(aiTextureType_AMBIENT_OCCLUSION, "Occlusion");
		TryAddTex(aiTextureType_LIGHTMAP, "Occlusion");
		TryAddTex(aiTextureType_EMISSION_COLOR, "Emissive");
		TryAddTex(aiTextureType_EMISSIVE, "Emissive");

		Out.Materials.push_back(std::move(Decoded));
	}
}

void BuildSkeletonFromBones(const aiScene* Scene, FDecodedSkeleton& Out)
{
	std::unordered_map<std::string, std::int32_t> NameToIndex;
	for (unsigned M = 0; M < Scene->mNumMeshes; ++M)
	{
		const aiMesh* Mesh = Scene->mMeshes[M];
		for (unsigned B = 0; B < Mesh->mNumBones; ++B)
		{
			const aiBone* Bone = Mesh->mBones[B];
			const std::string Name = Bone->mName.C_Str();
			if (NameToIndex.count(Name) != 0)
			{
				continue;
			}
			FDecodedBone Decoded;
			Decoded.Name = Name;
			Decoded.ParentIndex = -1;
			CopyAiMatrix(Decoded.BindLocal, Bone->mOffsetMatrix);
			NameToIndex.emplace(Name, static_cast<std::int32_t>(Out.Bones.size()));
			Out.Bones.push_back(std::move(Decoded));
		}
	}
}

void DecodeAnimations(const aiScene* Scene, FDecodedModelScene& Out)
{
	Out.Animations.reserve(Scene->mNumAnimations);
	for (unsigned A = 0; A < Scene->mNumAnimations; ++A)
	{
		const aiAnimation* Anim = Scene->mAnimations[A];
		FDecodedAnimation Decoded;
		Decoded.Name = Anim->mName.length > 0 ? Anim->mName.C_Str() : ("Anim_" + std::to_string(A));
		const double Ticks = Anim->mTicksPerSecond > 0.0 ? Anim->mTicksPerSecond : 25.0;
		Decoded.DurationSeconds = static_cast<float>(Anim->mDuration / Ticks);

		Decoded.Tracks.reserve(Anim->mNumChannels);
		for (unsigned C = 0; C < Anim->mNumChannels; ++C)
		{
			const aiNodeAnim* Channel = Anim->mChannels[C];
			FDecodedAnimTrack Track;
			Track.TargetBoneName = Channel->mNodeName.C_Str();

			unsigned KeyCount = Channel->mNumPositionKeys;
			if (Channel->mNumRotationKeys > KeyCount)
			{
				KeyCount = Channel->mNumRotationKeys;
			}
			if (Channel->mNumScalingKeys > KeyCount)
			{
				KeyCount = Channel->mNumScalingKeys;
			}
			Track.Keys.reserve(KeyCount);
			for (unsigned K = 0; K < KeyCount; ++K)
			{
				FDecodedAnimKey Key;
				if (K < Channel->mNumPositionKeys)
				{
					Key.Time = static_cast<float>(Channel->mPositionKeys[K].mTime / Ticks);
					Key.Translation[0] = Channel->mPositionKeys[K].mValue.x;
					Key.Translation[1] = Channel->mPositionKeys[K].mValue.y;
					Key.Translation[2] = Channel->mPositionKeys[K].mValue.z;
				}
				if (K < Channel->mNumRotationKeys)
				{
					Key.Time = static_cast<float>(Channel->mRotationKeys[K].mTime / Ticks);
					Key.Rotation[0] = Channel->mRotationKeys[K].mValue.x;
					Key.Rotation[1] = Channel->mRotationKeys[K].mValue.y;
					Key.Rotation[2] = Channel->mRotationKeys[K].mValue.z;
					Key.Rotation[3] = Channel->mRotationKeys[K].mValue.w;
				}
				if (K < Channel->mNumScalingKeys)
				{
					Key.Time = static_cast<float>(Channel->mScalingKeys[K].mTime / Ticks);
					Key.Scale[0] = Channel->mScalingKeys[K].mValue.x;
					Key.Scale[1] = Channel->mScalingKeys[K].mValue.y;
					Key.Scale[2] = Channel->mScalingKeys[K].mValue.z;
				}
				Track.Keys.push_back(Key);
			}
			Decoded.Tracks.push_back(std::move(Track));
		}
		Out.Animations.push_back(std::move(Decoded));
	}
}

bool DecodeWithAssimp(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePathHint,
	FDecodedModelScene& Out)
{
	Assimp::Importer Importer;
	const unsigned Flags =
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ImproveCacheLocality |
		aiProcess_LimitBoneWeights;

	const std::string Hint(SourcePathHint);
	const aiScene* Scene = nullptr;

	// glTF/OBJ/etc. reference external siblings (.bin, .mtl, textures). ReadFileFromMemory
	// only has the primary file bytes — Assimp cannot size-check buffers →
	// "Invalid byteLength exceeds size of actual data". Prefer disk path when available.
	if (!Hint.empty())
	{
		std::error_code ErrorCode;
		if (std::filesystem::is_regular_file(PathFromUtf8(Hint), ErrorCode) && !ErrorCode)
		{
			Scene = Importer.ReadFile(Hint.c_str(), Flags);
			if (!Scene)
			{
				MAHO_CORE_WARN(
					"MeshModelCodec: ReadFile('{}') failed ({}); falling back to memory",
					Hint,
					Importer.GetErrorString());
			}
		}
	}

	if (!Scene)
	{
		Scene = Importer.ReadFileFromMemory(
			Bytes,
			ByteCount,
			Flags,
			Hint.empty() ? nullptr : Hint.c_str());
	}

	if (!Scene || (Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !Scene->mRootNode)
	{
		MAHO_CORE_ERROR("MeshModelCodec: Assimp failed: {}", Importer.GetErrorString());
		return false;
	}

	Out = FDecodedModelScene{};
	Out.SourcePathHint = Hint;
	Out.Metadata.CoordinateSystem.Up = EModelAxis::Y;
	Out.Metadata.CoordinateSystem.Forward = EModelAxis::NegativeZ;
	Out.Metadata.CoordinateSystem.Handedness = EModelHandedness::Right;
	Out.Metadata.UnitScale = 1.f;

	DecodeMaterials(Scene, Out);

	Out.Meshes.reserve(Scene->mNumMeshes);
	for (unsigned I = 0; I < Scene->mNumMeshes; ++I)
	{
		FDecodedMesh Mesh;
		DecodeMesh(Scene->mMeshes[I], Mesh);
		if (Mesh.Name.empty())
		{
			Mesh.Name = "Mesh_" + std::to_string(I);
		}
		Out.Meshes.push_back(std::move(Mesh));
	}

	BuildSkeletonFromBones(Scene, Out.Skeleton);
	DecodeAnimations(Scene, Out);
	CollectNodes(Scene->mRootNode, -1, Out.Nodes);
	return true;
}

#endif // MAHO_WITH_ASSIMP

std::string AxisToString(EModelAxis Axis)
{
	switch (Axis)
	{
	case EModelAxis::X: return "X";
	case EModelAxis::Y: return "Y";
	case EModelAxis::Z: return "Z";
	case EModelAxis::NegativeX: return "-X";
	case EModelAxis::NegativeY: return "-Y";
	case EModelAxis::NegativeZ: return "-Z";
	default: return "Y";
	}
}

std::string EscapeJson(const std::string& Value)
{
	std::string Out;
	Out.reserve(Value.size() + 8);
	for (const char Ch : Value)
	{
		if (Ch == '\\' || Ch == '"')
		{
			Out.push_back('\\');
		}
		Out.push_back(Ch);
	}
	return Out;
}

std::string BuildAnimationGraphJson(
	const std::vector<std::string>& AnimSoftPaths,
	const std::string& DefaultSoftPath)
{
	std::ostringstream Oss;
	Oss << "{\n  \"Animations\": [";
	for (std::size_t I = 0; I < AnimSoftPaths.size(); ++I)
	{
		if (I != 0)
		{
			Oss << ", ";
		}
		Oss << "\"" << EscapeJson(AnimSoftPaths[I]) << "\"";
	}
	Oss << "],\n  \"DefaultAnimation\": \"" << EscapeJson(DefaultSoftPath) << "\"\n}\n";
	return Oss.str();
}

std::string BuildPrefabDocumentJson(
	const FDecodedModelMetadata& Meta,
	const std::vector<std::string>& MeshSoftPaths,
	const std::string& SkeletonSoftPath,
	const std::string& GraphSoftPath)
{
	std::ostringstream Oss;
	Oss << "{\n";
	Oss << "  \"Metadata\": {\n";
	Oss << "    \"CoordinateSystem\": {\n";
	Oss << "      \"Up\": \"" << AxisToString(Meta.CoordinateSystem.Up) << "\",\n";
	Oss << "      \"Forward\": \"" << AxisToString(Meta.CoordinateSystem.Forward) << "\",\n";
	Oss << "      \"Handedness\": \""
		<< (Meta.CoordinateSystem.Handedness == EModelHandedness::Left ? "Left" : "Right")
		<< "\"\n";
	Oss << "    },\n";
	Oss << "    \"UnitScale\": " << Meta.UnitScale << "\n";
	Oss << "  },\n";
	Oss << "  \"Meshes\": [";
	for (std::size_t I = 0; I < MeshSoftPaths.size(); ++I)
	{
		if (I != 0)
		{
			Oss << ", ";
		}
		Oss << "\"" << EscapeJson(MeshSoftPaths[I]) << "\"";
	}
	Oss << "],\n";
	Oss << "  \"Skeleton\": \"" << EscapeJson(SkeletonSoftPath) << "\",\n";
	Oss << "  \"AnimationGraph\": \"" << EscapeJson(GraphSoftPath) << "\"\n";
	Oss << "}\n";
	return Oss.str();
}

} // namespace

std::string GetExtensionLower(std::string_view Path)
{
	const auto Slash = Path.find_last_of("/\\");
	const std::string_view Name = Slash == std::string_view::npos ? Path : Path.substr(Slash + 1);
	const auto Dot = Name.find_last_of('.');
	if (Dot == std::string_view::npos || Dot + 1 >= Name.size())
	{
		return {};
	}
	std::string Ext(Name.substr(Dot + 1));
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char C)
	{
		return static_cast<char>(std::tolower(C));
	});
	return Ext;
}

bool IsModelExtension(std::string_view Ext)
{
	return Ext == "fbx" || Ext == "gltf" || Ext == "glb" || Ext == "obj" || Ext == "dae" ||
		Ext == "blend" || Ext == "3ds" || Ext == "ase" || Ext == "ply" || Ext == "stl" ||
		Ext == "pmx" || Ext == "pmd";
}

bool MatchesModelSourcePath(std::string_view Path)
{
	return IsModelExtension(GetExtensionLower(Path));
}

bool DecodeFromMemory(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePathHint,
	FDecodedModelScene& Out)
{
	if (!Bytes || ByteCount == 0)
	{
		return false;
	}

#if defined(MAHO_WITH_ASSIMP) && MAHO_WITH_ASSIMP
	return DecodeWithAssimp(Bytes, ByteCount, SourcePathHint, Out);
#else
	(void)SourcePathHint;
	(void)Out;
	MAHO_CORE_ERROR("MeshModelCodec: Assimp disabled (MAHO_WITH_ASSIMP=0); cannot decode model");
	return false;
#endif
}

bool PrepareModelImport(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePathHint,
	FPreparedModelImport& Out)
{
	Out = {};
	if (!DecodeFromMemory(Bytes, ByteCount, SourcePathHint, Out.Scene))
	{
		return false;
	}

	auto ResolveTextureDiskPath = [&](const std::string& RelativeOrAbsolute) -> std::string
	{
		if (RelativeOrAbsolute.empty() || RelativeOrAbsolute[0] == '*')
		{
			return {};
		}
		std::error_code ErrorCode;
		const std::filesystem::path AsPath = PathFromUtf8(RelativeOrAbsolute);
		if (AsPath.is_absolute())
		{
			return PathToUtf8(std::filesystem::weakly_canonical(AsPath, ErrorCode));
		}
		if (Out.Scene.SourcePathHint.empty())
		{
			return RelativeOrAbsolute;
		}
		const std::filesystem::path BaseDir = PathFromUtf8(Out.Scene.SourcePathHint).parent_path();
		return PathToUtf8(std::filesystem::weakly_canonical(BaseDir / AsPath, ErrorCode));
	};

	for (const FDecodedMaterial& Mat : Out.Scene.Materials)
	{
		for (const FDecodedTextureRef& TexRef : Mat.Textures)
		{
			FPreparedTextureImage Prepared;
			Prepared.bForceLinear = TexRef.SlotName == "Normal"
				|| TexRef.SlotName == "MetallicRoughness"
				|| TexRef.SlotName == "Occlusion";

			std::vector<std::uint8_t> FileBytes;
			if (!TexRef.EmbeddedBytes.empty())
			{
				Prepared.CacheKey = "embedded:" + TexRef.SourcePath;
				FileBytes = TexRef.EmbeddedBytes;
				Prepared.DecodeHint = TexRef.SourcePath;
				if (Prepared.DecodeHint.empty() || Prepared.DecodeHint[0] == '*')
				{
					Prepared.DecodeHint = "embedded.png";
				}
			}
			else
			{
				Prepared.CacheKey = ResolveTextureDiskPath(TexRef.SourcePath);
				if (Prepared.CacheKey.empty())
				{
					continue;
				}
				if (Out.Textures.find(Prepared.CacheKey) != Out.Textures.end())
				{
					continue;
				}

				std::ifstream File(PathFromUtf8(Prepared.CacheKey), std::ios::binary | std::ios::ate);
				if (!File)
				{
					MAHO_CORE_WARN("MeshModelCodec: cannot open texture '{}'", Prepared.CacheKey);
					continue;
				}
				const std::streamoff Size = File.tellg();
				if (Size <= 0)
				{
					continue;
				}
				File.seekg(0, std::ios::beg);
				FileBytes.resize(static_cast<std::size_t>(Size));
				if (!File.read(reinterpret_cast<char*>(FileBytes.data()), Size))
				{
					MAHO_CORE_WARN("MeshModelCodec: failed reading texture '{}'", Prepared.CacheKey);
					continue;
				}
				Prepared.DecodeHint = Prepared.CacheKey;
			}

			if (Out.Textures.find(Prepared.CacheKey) != Out.Textures.end())
			{
				continue;
			}

			if (!TextureImageCodec::DecodeFromMemory(
					FileBytes.data(),
					FileBytes.size(),
					Prepared.DecodeHint,
					Prepared.Image))
			{
				MAHO_CORE_WARN("MeshModelCodec: decode failed for texture '{}'", Prepared.DecodeHint);
				continue;
			}
			if (Prepared.bForceLinear)
			{
				Prepared.Image.bSRGB = false;
			}
			Prepared.SerializedSourceBytes = std::move(FileBytes);
			Out.Textures.emplace(Prepared.CacheKey, std::move(Prepared));
		}
	}

	return true;
}

bool ApplyDecodedModelScene(
	FDecodedModelScene&& Scene,
	FResourceSystem& Resources,
	const std::string& PackagePath,
	FPrefab& Prefab,
	const std::unordered_map<std::string, FPreparedTextureImage>* PreparedTextures)
{
	// On failure, drop siblings created here (Prefab root is aborted by ProcessReadyIO).
	struct FApplyRollback
	{
		FResourceSystem& Resources;
		std::vector<FResource*> Created;
		bool bCommitted = false;

		void Track(FResource* Object)
		{
			if (Object)
			{
				Created.push_back(Object);
			}
		}

		~FApplyRollback()
		{
			if (bCommitted)
			{
				return;
			}
			for (FResource* Obj : Created)
			{
				Resources.UnregisterResource(Obj);
			}
		}
	};

	FApplyRollback Rollback{Resources, {}, false};

	std::vector<std::string> MeshMaterialPaths;
	std::vector<FMaterial*> Materials;
	Materials.reserve(Scene.Materials.size());

	// Dedup texture imports by absolute path / embedded key -> AssetPath string.
	std::unordered_map<std::string, std::string> ImportedTextures;

	auto ResolveTextureDiskPath = [&](const std::string& RelativeOrAbsolute) -> std::string
	{
		if (RelativeOrAbsolute.empty() || RelativeOrAbsolute[0] == '*')
		{
			return {};
		}
		std::error_code ErrorCode;
		const std::filesystem::path AsPath = PathFromUtf8(RelativeOrAbsolute);
		if (AsPath.is_absolute())
		{
			return PathToUtf8(std::filesystem::weakly_canonical(AsPath, ErrorCode));
		}
		if (Scene.SourcePathHint.empty())
		{
			return RelativeOrAbsolute;
		}
		const std::filesystem::path BaseDir = PathFromUtf8(Scene.SourcePathHint).parent_path();
		return PathToUtf8(std::filesystem::weakly_canonical(BaseDir / AsPath, ErrorCode));
	};

	auto ImportTextureSibling = [&](const FDecodedTextureRef& TexRef) -> std::string
	{
		std::string CacheKey;
		std::vector<std::uint8_t> Bytes;
		std::string DecodeHint = TexRef.SourcePath;
		bool bForceLinear = TexRef.SlotName == "Normal"
			|| TexRef.SlotName == "MetallicRoughness"
			|| TexRef.SlotName == "Occlusion";

		if (!TexRef.EmbeddedBytes.empty())
		{
			CacheKey = "embedded:" + TexRef.SourcePath;
			Bytes = TexRef.EmbeddedBytes;
			if (DecodeHint.empty() || DecodeHint[0] == '*')
			{
				DecodeHint = "embedded.png";
			}
		}
		else
		{
			CacheKey = ResolveTextureDiskPath(TexRef.SourcePath);
			if (CacheKey.empty())
			{
				return {};
			}
			const auto It = ImportedTextures.find(CacheKey);
			if (It != ImportedTextures.end())
			{
				return It->second;
			}
			DecodeHint = CacheKey;
		}

		const auto Existing = ImportedTextures.find(CacheKey);
		if (Existing != ImportedTextures.end())
		{
			return Existing->second;
		}

		FDecodedImage Image;
		std::vector<std::uint8_t> SerializedBytes;
		if (PreparedTextures)
		{
			const auto PrepIt = PreparedTextures->find(CacheKey);
			if (PrepIt == PreparedTextures->end())
			{
				MAHO_CORE_WARN("MeshModelCodec: missing prepared texture '{}'", CacheKey);
				return {};
			}
			Image = PrepIt->second.Image;
			SerializedBytes = PrepIt->second.SerializedSourceBytes;
			DecodeHint = PrepIt->second.DecodeHint;
			if (PrepIt->second.bForceLinear)
			{
				bForceLinear = true;
			}
		}
		else
		{
			if (Bytes.empty())
			{
				std::ifstream File(PathFromUtf8(CacheKey), std::ios::binary | std::ios::ate);
				if (!File)
				{
					MAHO_CORE_WARN("MeshModelCodec: cannot open texture '{}'", CacheKey);
					return {};
				}
				const std::streamoff Size = File.tellg();
				if (Size <= 0)
				{
					return {};
				}
				File.seekg(0, std::ios::beg);
				Bytes.resize(static_cast<std::size_t>(Size));
				if (!File.read(reinterpret_cast<char*>(Bytes.data()), Size))
				{
					MAHO_CORE_WARN("MeshModelCodec: failed reading texture '{}'", CacheKey);
					return {};
				}
			}

			if (!TextureImageCodec::DecodeFromMemory(Bytes.data(), Bytes.size(), DecodeHint, Image))
			{
				MAHO_CORE_WARN("MeshModelCodec: decode failed for texture '{}'", DecodeHint);
				return {};
			}
			SerializedBytes = std::move(Bytes);
		}

		const std::string Stem = SanitizeObjectName(
			PathToUtf8(PathFromUtf8(DecodeHint).stem()),
			"Texture");
		FTexture2D* Texture = CreateRegisteredResource<FTexture2D>(
			Resources,
			PackagePath,
			Stem,
			EAssetType::Texture2D,
			CacheKey);
		if (!Texture)
		{
			return {};
		}
		Rollback.Track(Texture);
		if (bForceLinear)
		{
			Image.bSRGB = false;
		}
		if (!TextureImageCodec::ApplyDecodedToTexture(*Texture, std::move(Image)))
		{
			MAHO_CORE_WARN("MeshModelCodec: ApplyDecodedToTexture failed for '{}'", Stem);
			return {};
		}
		if (!SerializedBytes.empty())
		{
			Texture->SetSerializedSource(DecodeHint, std::move(SerializedBytes));
		}
		Texture->MarkCpuReady();
		std::string AssetPath = Resources.MakeAssetCatalogKey(PackagePath, Texture->GetName());
		ImportedTextures.emplace(CacheKey, AssetPath);
		return AssetPath;
	};

	for (std::size_t I = 0; I < Scene.Materials.size(); ++I)
	{
		const FDecodedMaterial& Src = Scene.Materials[I];
		const std::string Name = SanitizeObjectName(Src.Name, "Material_" + std::to_string(I));
		FMaterial* Mat = CreateRegisteredResource<FMaterial>(
			Resources,
			PackagePath,
			Name,
			EAssetType::Material,
			{});
		if (!Mat)
		{
			return false;
		}
		Rollback.Track(Mat);
		Mat->BaseColorFactor[0] = Src.BaseColorFactor[0];
		Mat->BaseColorFactor[1] = Src.BaseColorFactor[1];
		Mat->BaseColorFactor[2] = Src.BaseColorFactor[2];
		Mat->BaseColorFactor[3] = Src.BaseColorFactor[3];
		Mat->MetallicFactor = Src.MetallicFactor;
		Mat->RoughnessFactor = Src.RoughnessFactor;
		Mat->EmissiveFactor[0] = Src.EmissiveFactor[0];
		Mat->EmissiveFactor[1] = Src.EmissiveFactor[1];
		Mat->EmissiveFactor[2] = Src.EmissiveFactor[2];
		for (const FDecodedTextureRef& Tex : Src.Textures)
		{
			if (Tex.SourcePath.empty() && Tex.EmbeddedBytes.empty())
			{
				continue;
			}
			std::string Soft = ImportTextureSibling(Tex);
			if (Soft.empty())
			{
				continue;
			}
			if (Tex.SlotName == "BaseColor")
			{
				Mat->SetBaseColorTexture(std::move(Soft));
			}
			else if (Tex.SlotName == "Normal")
			{
				Mat->SetNormalTexture(std::move(Soft));
			}
			else if (Tex.SlotName == "MetallicRoughness")
			{
				Mat->SetMetallicRoughnessTexture(std::move(Soft));
			}
			else if (Tex.SlotName == "Occlusion")
			{
				Mat->SetOcclusionTexture(std::move(Soft));
			}
			else if (Tex.SlotName == "Emissive")
			{
				Mat->SetEmissiveTexture(std::move(Soft));
			}
		}
		Mat->MarkCpuReady();
		Materials.push_back(Mat);
	}

	std::vector<std::string> MeshSoftPathStrings;
	MeshSoftPathStrings.reserve(Scene.Meshes.size());

	for (std::size_t I = 0; I < Scene.Meshes.size(); ++I)
	{
		FDecodedMesh& Src = Scene.Meshes[I];
		const std::string Name = SanitizeObjectName(Src.Name, "Mesh_" + std::to_string(I));
		FStaticMesh* Mesh = CreateRegisteredResource<FStaticMesh>(
			Resources,
			PackagePath,
			Name,
			EAssetType::Mesh,
			{});
		if (!Mesh)
		{
			return false;
		}
		Rollback.Track(Mesh);
		Mesh->SetCpuGeometry(
			std::move(Src.Positions),
			std::move(Src.Normals),
			std::move(Src.UVs),
			std::move(Src.Indices));
		if (Src.MaterialIndex >= 0 &&
			static_cast<std::size_t>(Src.MaterialIndex) < Materials.size() &&
			Materials[static_cast<std::size_t>(Src.MaterialIndex)])
		{
			Mesh->SetMaterial(
				Resources.MakeAssetCatalogKey(PackagePath, Materials[static_cast<std::size_t>(Src.MaterialIndex)]->GetName()));
		}
		Mesh->MarkCpuReady();
		MeshSoftPathStrings.push_back(Resources.MakeAssetCatalogKey(PackagePath, Mesh->GetName()));
	}

	std::string SkeletonSoftPath;
	std::string SkeletonPath;
	if (!Scene.Skeleton.IsEmpty())
	{
		FSkeleton* Skeleton = CreateRegisteredResource<FSkeleton>(
			Resources,
			PackagePath,
			"Skeleton",
			EAssetType::Skeleton,
			{});
		if (!Skeleton)
		{
			return false;
		}
		Rollback.Track(Skeleton);
		std::vector<FSkeletonBone> Bones;
		Bones.reserve(Scene.Skeleton.Bones.size());
		for (const FDecodedBone& Bone : Scene.Skeleton.Bones)
		{
			FSkeletonBone OutBone;
			OutBone.Name = Bone.Name;
			OutBone.ParentIndex = Bone.ParentIndex;
			CopyMat4(OutBone.BindLocal, Bone.BindLocal);
			Bones.push_back(std::move(OutBone));
		}
		Skeleton->SetBones(std::move(Bones));
		Skeleton->MarkCpuReady();
		SkeletonPath = Resources.MakeAssetCatalogKey(PackagePath, Skeleton->GetName());
		SkeletonSoftPath = SkeletonPath;
	}

	std::vector<std::string> AnimSoftPathStrings;
	AnimSoftPathStrings.reserve(Scene.Animations.size());
	for (std::size_t I = 0; I < Scene.Animations.size(); ++I)
	{
		FDecodedAnimation& Src = Scene.Animations[I];
		const std::string Name = SanitizeObjectName(Src.Name, "Animation_" + std::to_string(I));
		FAnimation* Anim = CreateRegisteredResource<FAnimation>(
			Resources,
			PackagePath,
			Name,
			EAssetType::Animation,
			{});
		if (!Anim)
		{
			return false;
		}
		Rollback.Track(Anim);
		Anim->SetDurationSeconds(Src.DurationSeconds);
		if (!SkeletonPath.empty())
		{
			Anim->SetSkeleton(SkeletonPath);
		}
		std::vector<FAnimationTrack> Tracks;
		Tracks.reserve(Src.Tracks.size());
		for (FDecodedAnimTrack& Track : Src.Tracks)
		{
			FAnimationTrack OutTrack;
			OutTrack.TargetBoneName = std::move(Track.TargetBoneName);
			OutTrack.Keys.reserve(Track.Keys.size());
			for (const FDecodedAnimKey& Key : Track.Keys)
			{
				FAnimationKey OutKey;
				OutKey.Time = Key.Time;
				OutKey.Translation[0] = Key.Translation[0];
				OutKey.Translation[1] = Key.Translation[1];
				OutKey.Translation[2] = Key.Translation[2];
				OutKey.Rotation[0] = Key.Rotation[0];
				OutKey.Rotation[1] = Key.Rotation[1];
				OutKey.Rotation[2] = Key.Rotation[2];
				OutKey.Rotation[3] = Key.Rotation[3];
				OutKey.Scale[0] = Key.Scale[0];
				OutKey.Scale[1] = Key.Scale[1];
				OutKey.Scale[2] = Key.Scale[2];
				OutTrack.Keys.push_back(OutKey);
			}
			Tracks.push_back(std::move(OutTrack));
		}
		Anim->SetTracks(std::move(Tracks));
		Anim->MarkCpuReady();
		AnimSoftPathStrings.push_back(Resources.MakeAssetCatalogKey(PackagePath, Anim->GetName()));
	}

	FAnimationGraph* Graph = CreateRegisteredResource<FAnimationGraph>(
		Resources,
		PackagePath,
		"AnimationGraph",
		EAssetType::AnimationGraph,
		{});
	if (!Graph)
	{
		return false;
	}
	Rollback.Track(Graph);
	const std::string DefaultAnim =
		AnimSoftPathStrings.empty() ? std::string{} : AnimSoftPathStrings.front();
	Graph->SetDocumentJson(BuildAnimationGraphJson(AnimSoftPathStrings, DefaultAnim));
	Graph->MarkCpuReady();
	const std::string GraphSoftPath = Resources.MakeAssetCatalogKey(PackagePath, Graph->GetName());

	Prefab.SetDocumentJson(BuildPrefabDocumentJson(
		Scene.Metadata,
		MeshSoftPathStrings,
		SkeletonSoftPath,
		GraphSoftPath));
	Rollback.bCommitted = true;
	return true;
}

} // namespace MeshModelCodec
} // namespace Maho
