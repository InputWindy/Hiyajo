#pragma once

#include "ImGuiRenderApi.h"
#include <Engine/Layer.h>
#include <Render.h>

#include <memory>

namespace Maho
{

/**
 * ImGui render feature - the render-side half of the ImGui integration. The CPU
 * side (FImGuiSystem, owned by FRender) builds the UI frame here in IInitViews
 * (UE InitViews analogue: game-side UI state -> render-side ImDrawData); this
 * feature draws that data in IRenderUI over the shared SceneColor (LoadOp Load,
 * after the scene), submitted before the frame feature's present blit.
 */
class MAHO_IMGUIRENDER_API FImGuiRenderFeature : public FLayer<IInitViews, IRenderUI>
{
MAHO_DECLARE_LAYER(FImGuiRenderFeature, "ImGuiRender.dll");

	FImGuiRenderFeature();
	~FImGuiRenderFeature() override;

public:
	void InitViews(FRender& R) override;
	void RenderUI(FRender& R) override;

private:
	bool EnsureBackend(FRender& R);
	struct FData;
	std::unique_ptr<FData> Data;
};

} // namespace Maho
