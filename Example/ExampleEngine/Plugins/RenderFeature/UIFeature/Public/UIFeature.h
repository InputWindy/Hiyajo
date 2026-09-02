#pragma once

#include "UIFeatureApi.h"
#include <Engine/Layer.h>
#include <Render.h>

#include <memory>

namespace Maho
{

/**
 * ImGui render feature - the render-side half of the UI integration. The CPU
 * side (the UI engine layer's ITick) builds the ImGui frame and pushes the draw
 * data to FScene via the sink; this feature draws that data in IRenderUI over
 * the shared SceneColor (LoadOp Load, after the scene), submitted before the
 * frame feature's present blit.
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
