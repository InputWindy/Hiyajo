#pragma once

#include "ImGuiRenderApi.h"
#include <Engine/Layer.h>
#include <Render.h>

#include <memory>

namespace Maho
{

/**
 * ImGui render feature - the render-side half of the ImGui integration. FRender
 * owns the FImGuiSystem host and builds the UI (NewFrame) BEFORE the render
 * graph executes; this feature records that same frame's draw data into the
 * shared SceneColor (LoadOp Load, over the scene) with the official
 * imgui_impl_vulkan backend, submitted before the frame feature's present blit.
 */
class MAHO_IMGUIRENDER_API FImGuiRenderFeature : public FLayer<IBeginRender, IRender, IEndRender>
{
MAHO_DECLARE_LAYER(FImGuiRenderFeature, "ImGuiRender.dll");

	FImGuiRenderFeature();
	~FImGuiRenderFeature() override;

public:
	void BeginRender(FRender& R) override;
	void Render(FRender& R) override;
	void EndRender(FRender& R) override;

private:
	bool EnsureBackend(FRender& R);
	struct FData;
	std::unique_ptr<FData> Data;
};

} // namespace Maho
