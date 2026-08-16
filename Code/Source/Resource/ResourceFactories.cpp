#include "Resource/ResourceFactories.h"

#include "Resource/ResourceCasset.h"
#include "Resource/ResourceTypes.h"

#include <ResourceSystem.h>

namespace Maho
{

void RegisterResourceFactories()
{
	FResourceSystem* R = Detail::GetResourceSystem();
	if (!R)
		return;

	RegisterCassetPackageCodec(*R);

	R->RegisterResourceFactory(EAssetType::Texture2D, []
	R->RegisterResourceFactory(EAssetType::Texture2D, {
	R->RegisterResourceFactory(EAssetType::Texture2D, 	return new FTexture2D(N, T, S);
	R->RegisterResourceFactory(EAssetType::Texture2D, });
	R->RegisterResourceFactory(EAssetType::Texture3D, []
	R->RegisterResourceFactory(EAssetType::Texture3D, {
	R->RegisterResourceFactory(EAssetType::Texture3D, 	return new FTexture3D(N, T, S);
	R->RegisterResourceFactory(EAssetType::Texture3D, });
	R->RegisterResourceFactory(EAssetType::TextureCube, []
	R->RegisterResourceFactory(EAssetType::TextureCube, {
	R->RegisterResourceFactory(EAssetType::TextureCube, 	return new FTextureCube(N, T, S);
	R->RegisterResourceFactory(EAssetType::TextureCube, });
	R->RegisterResourceFactory(EAssetType::TextureCubeArray, []
	R->RegisterResourceFactory(EAssetType::TextureCubeArray, {
	R->RegisterResourceFactory(EAssetType::TextureCubeArray, 	return new FTextureCubeArray(N, T, S);
	R->RegisterResourceFactory(EAssetType::TextureCubeArray, });
	R->RegisterResourceFactory(EAssetType::Texture2DArray, []
	R->RegisterResourceFactory(EAssetType::Texture2DArray, {
	R->RegisterResourceFactory(EAssetType::Texture2DArray, 	return new FTexture2DArray(N, T, S);
	R->RegisterResourceFactory(EAssetType::Texture2DArray, });
	R->RegisterResourceFactory(EAssetType::Mesh, []
	R->RegisterResourceFactory(EAssetType::Mesh, {
	R->RegisterResourceFactory(EAssetType::Mesh, 	return new FStaticMesh(N, T, S);
	R->RegisterResourceFactory(EAssetType::Mesh, });
	R->RegisterResourceFactory(EAssetType::Material, []
	R->RegisterResourceFactory(EAssetType::Material, {
	R->RegisterResourceFactory(EAssetType::Material, 	return new FMaterial(N, T, S);
	R->RegisterResourceFactory(EAssetType::Material, });
	R->RegisterResourceFactory(EAssetType::Skeleton, []
	R->RegisterResourceFactory(EAssetType::Skeleton, {
	R->RegisterResourceFactory(EAssetType::Skeleton, 	return new FSkeleton(N, T, S);
	R->RegisterResourceFactory(EAssetType::Skeleton, });
	R->RegisterResourceFactory(EAssetType::Animation, []
	R->RegisterResourceFactory(EAssetType::Animation, {
	R->RegisterResourceFactory(EAssetType::Animation, 	return new FAnimation(N, T, S);
	R->RegisterResourceFactory(EAssetType::Animation, });
	R->RegisterResourceFactory(EAssetType::AnimationGraph, []
	R->RegisterResourceFactory(EAssetType::AnimationGraph, {
	R->RegisterResourceFactory(EAssetType::AnimationGraph, 	return new FAnimationGraph(N, T, S);
	R->RegisterResourceFactory(EAssetType::AnimationGraph, });
	R->RegisterResourceFactory(EAssetType::Prefab, []
	R->RegisterResourceFactory(EAssetType::Prefab, {
	R->RegisterResourceFactory(EAssetType::Prefab, 	return new FPrefab(N, T, S);
	R->RegisterResourceFactory(EAssetType::Prefab, });
}

namespace
{

struct FResourceFactoryRegistrar
{
	FResourceFactoryRegistrar()
	{
		RegisterResourceFactories();
	}
} GResourceFactoryRegistrar;

} // namespace

} // namespace Maho
