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

	/** The latest ImGui draw data (ImDrawData*, backend-agnostic void*), pushed by
	 *  the ImGui host each frame through a registered sink. The UI render feature
	 *  reads + draws it, like this feature draws SceneColor. */
	void SetImGuiDrawData(void* DrawData) { ImGuiDrawData = DrawData; }
	[[nodiscard]] void* GetImGuiDrawData() const { return ImGuiDrawData; }

	void BeginRender(FRender& R) override;
	void Render(FRender& R) override;
	void EndRender(FRender& R) override;

private:
	void EnsureTargets(FRender& R);

	FRHICommandList* RenderList = nullptr;   // acquired in BeginRender, recorded in Render, submitted in EndRender

	FRDGTextureRef SceneColor;
	FRDGTextureRef SceneDepth;
	void* ImGuiDrawData = nullptr;   // ImDrawData* from the ImGui host
	std::uint32_t CachedWidth = 0;
	std::uint32_t CachedHeight = 0;
	bool bTargetsNeedTransition = true;   // fresh targets need Common -> RenderTarget once
};

} // namespace Scene
} // namespace Maho
