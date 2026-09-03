#pragma once

#include "SceneApi.h"

#include <Maho.h>
#include <Engine/Layer.h>
#include <Render.h>
#include <RDG.h>
#include <RenderDrawList.h>

#include <cstdint>

namespace Maho
{
namespace Scene
{

class FScene;

/** Global scene resource feature accessor (Scene.dll). */
MAHO_SCENE_API FScene* GetScene();

/**
 * FScene - the global render resource feature (UE-aligned). Mounts the render
 * record stages; cross-frame owns the shared scene targets (SceneColor /
 * SceneDepth). Other features read them through Scene::GetScene() - no named
 * slots in FRender. Targets are rebuilt when the swapchain extent changes.
 * (The present/blit lives in the Frame feature, not here.)
 */
class MAHO_SCENE_API FScene : public FLayer<IBeginRender, IRender, IEndRender>
{
	MAHO_DECLARE_LAYER(FScene, "RenderScene.dll");

public:
	FScene();

	[[nodiscard]] FRDGTextureRef GetSceneColor() const { return SceneColor; }
	[[nodiscard]] FRDGTextureRef GetSceneDepth() const { return SceneDepth; }

	/**
	 * Test producer of the draw protocol: the hardcoded fullscreen triangle. AddPass
	 * consumes the list as-is and never knows it came from here (a real scene
	 * renderer replaces it later). The triangle has no vertex buffer -- the vertex
	 * shader generates its 3 positions from gl_VertexIndex.
	 */
	[[nodiscard]] const FDrawList& GetTriangleDrawList() const { return TriangleDrawList; }

	void BeginRender(FRender& R) override;
	void Render(FRender& R) override;
	void EndRender(FRender& R) override;

private:
	void EnsureTargets(FRender& R);

	FRDGTextureRef SceneColor;
	FRDGTextureRef SceneDepth;
	FDrawList TriangleDrawList;   // hardcoded triangle draw protocol (test producer)
	std::uint32_t CachedWidth = 0;
	std::uint32_t CachedHeight = 0;
	bool bTargetsNeedTransition = true;   // fresh targets need Common -> RenderTarget once
};

} // namespace Scene
} // namespace Maho
