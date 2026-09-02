#pragma once

#include "UIFeatureApi.h"
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
class MAHO_UIFEATURE_API FUIFeature : public FLayer<IInitViews, IRenderUI>
{
MAHO_DECLARE_LAYER(FUIFeature, "UIFeature.dll");

	FUIFeature();
	~FUIFeature() override;

public:
	void InitViews(FRender& R) override;
	void RenderUI(FRender& R) override;

private:
	bool EnsureBackend(FRender& R);
	struct FData;
	std::unique_ptr<FData> Data;
};

} // namespace Maho
