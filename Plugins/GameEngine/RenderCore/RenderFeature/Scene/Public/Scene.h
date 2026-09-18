#pragma once

#include "SceneApi.h"

#include <Maho.h>
#include <Engine/Frame.h>
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
 *
 * It also owns the FRAME BOUNDARY: IBeginRender opens the swapchain frame
 * (BeginSwapchainFrame: fence wait + acquire + begin frame command list + pool
 * advance) and IPresent closes it (submit the frame's passes, blit, then
 * EndSwapchainFrame). Both are nodes of this collector's graph, so the ordering
 * "next frame's head after this frame's tail" is a declared cross-frame edge
 * (see the ctor) rather than a Wait() on the host chain.
 */
class MAHO_SCENE_API FScene : public FFrameExtension, public IPipeline<IBeginRender, IRender, IEndRender, IPresent, IPreUnInstall>
{
	MAHO_DECLARE_FRAME(FScene);

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

	/** The frame HEAD: opens the swapchain frame, then (re)builds the shared targets. Everything that
	 *  acquires a command list or allocates from the resource pool must be ordered after this. */
	void BeginRender(FRender& R, FRenderContext& Frame) override;
	void Render(FRender& R, FRenderContext& Frame) override;
	void EndRender(FRender& R, FRenderContext& Frame) override;

	/**
	 * The frame's last stage, and its single submission point: submit every pass recorded this frame,
	 * record the blit of the present target, then close + submit the frame command list and present.
	 * Both the submission point and the frame close live here (rather than on the host's IEndFrame)
	 * because the frame's recorded-pass table is per-slot state of THIS graph -- the host chain's slot
	 * comes from the engine graph's counter and must not index it -- and because closing the frame
	 * list requires the recording that this same stage just did.
	 */
	void Present(FRender& R, FRenderContext& Frame) override;

	/**
	 * Release the shared targets BEFORE this module unloads. FScene is a sub-plugin of
	 * FRender (unloaded during FRender's shutdown), and SceneColor/SceneDepth are
	 * cataloged in the resource system -- which outlives us. Leaving them behind makes
	 * the resource system destroy objects whose owning module is already gone (the
	 * validated crash: a sub-plugin unloaded before FResourceSystem::Shutdown).
	 */
	void PreUnInstall(FRender& R, FRenderContext& Frame) override;

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
