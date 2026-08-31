#pragma once

#include "SceneApi.h"

#include <Maho.h>
#include <Engine/Layer.h>
#include <Render.h>
#include <RDG.h>

#include <cstdint>

namespace Maho
{
namespace Scene
{

class FScene;

/** Global scene resource feature accessor (Scene.dll). */
MAHO_SCENE_API FScene* GetScene();

/**
 * FScene - the global render resource feature (UE-aligned). Mounts all render
 * stages; cross-frame owns the shared scene targets (SceneColor / SceneDepth).
 * Other features read them through Scene::GetScene() - no named slots in
 * FRender. Targets are rebuilt when the swapchain extent changes.
 */
class MAHO_SCENE_API FScene : public FLayer<IBeginRender, IRender, IEndRender, IPresent>
{
	MAHO_DECLARE_LAYER(FScene, "Scene.dll");

public:
	FScene();

	[[nodiscard]] FRDGTextureRef GetSceneColor() const { return SceneColor; }
	[[nodiscard]] FRDGTextureRef GetSceneDepth() const { return SceneDepth; }

	void BeginRender(FRender& R) override;
	void Render(FRender& R) override;
	void EndRender(FRender& R) override;
	void Present(FRender& R) override;

private:
	void EnsureTargets(FRender& R);

	FRDGTextureRef SceneColor;
	FRDGTextureRef SceneDepth;
	std::uint32_t CachedWidth = 0;
	std::uint32_t CachedHeight = 0;
};

} // namespace Scene
} // namespace Maho
