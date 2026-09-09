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
 * (The present/blit lives in the UI feature's IPresent, not here.)
 */
class MAHO_SCENE_API FScene : public FLayer<IBeginRender, IRender, IEndRender>
{
	MAHO_DECLARE_LAYER(FScene);

public:
	FScene();
	~FScene() override;

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

	/**
	 * SceneColor doubles as a render target (scene clears/draws into it) AND a sampled
	 * source (the editor viewport reads it as a mirror). These need opposite layouts
	 * (COLOR_ATTACHMENT_OPTIMAL vs SHADER_READ_ONLY_OPTIMAL), so the layout must be
	 * explicitly toggled around each use. The scene itself owns the RenderTarget side
	 * (in Render(), before writing); the sampler side (typically the editing UI compose
	 * pass) calls TransitionSceneColorForSampling / TransitionSceneColorForRendering to
	 * flip to/from SHADER_READ_ONLY across the frame -- otherwise the RHI's descriptor
	 * writes hardcode SHADER_READ_ONLY and the validation layer errors on an image still
	 * in COLOR_ATTACHMENT_OPTIMAL (or, on a never-transitioned fresh target, UNDEFINED).
	 */
	void TransitionSceneColorForSampling(FRender& R);
	void TransitionSceneColorForRendering(FRender& R);

private:
	enum class ESceneColorLayout : std::uint8_t
	{
		Undefined,     // never transitioned (fresh / before first Render)
		RenderTarget,  // last left as COLOR_ATTACHMENT_OPTIMAL
		ShaderResource,// last left as SHADER_READ_ONLY_OPTIMAL
	};

	void EnsureTargets(FRender& R);

	FRDGTextureRef SceneColor;
	FRDGTextureRef SceneDepth;
	FDrawList TriangleDrawList;   // hardcoded triangle draw protocol (test producer)
	std::uint32_t CachedWidth = 0;
	std::uint32_t CachedHeight = 0;
	bool bTargetsNeedTransition = true;   // fresh targets need Common -> RenderTarget once
	ESceneColorLayout SceneColorLayout = ESceneColorLayout::Undefined;
};

} // namespace Scene
} // namespace Maho
