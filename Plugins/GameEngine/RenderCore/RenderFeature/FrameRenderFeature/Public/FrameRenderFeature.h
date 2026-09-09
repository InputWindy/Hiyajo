#pragma once

#include "FrameRenderFeatureApi.h"
#include <Engine/Layer.h>
#include <Render.h>
#include <RDG.h>

namespace Maho
{

/**
 * Frame present feature -- the ENGINE'S SINGLE present point, decoupled from any
 * one UI feature's off-screen compositing. It owns NO ImGui state and draws NOTHING:
 * it only reads this frame's present target (FRender::GetPresentTarget, a slot any
 * UI feature writes at the end of its RenderUI) and blits it to the swapchain.
 *
 * This decoupling lets an editor UI feature (Type=Editor plugin) set an EditorRT
 * as the present target in an editor build while a runtime build still presents
 * the GameUI composite -- the present point is fixed, the target is switchable.
 *
 * IPresent is the LAST stage of FRenderStages. The feature declares reverse edges
 * (BlockOn) from each producer so it always runs after the producer's final
 * composite stage; a producer that is not installed degrades gracefully (the edge
 * is skipped). See the design: the frame feature waits on every render feature's
 * IEndRender via the producers' own declarations.
 */
class MAHO_FRAMERENDERFEATURE_API FFrameRenderFeature : public FLayer<IPresent>
{
MAHO_DECLARE_LAYER(FFrameRenderFeature);

public:
	FFrameRenderFeature();

	/** Blit the current present target to the swapchain (no-op when unset). */
	void Present(FRender& R) override;
};

} // namespace Maho
