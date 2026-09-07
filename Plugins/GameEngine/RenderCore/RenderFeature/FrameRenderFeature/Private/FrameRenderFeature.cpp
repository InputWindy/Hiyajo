#include "FrameRenderFeature.h"

namespace Maho
{

FFrameRenderFeature::FFrameRenderFeature()
{
	// The present point is the LAST frame stage. It must run only after every
	// render feature finished compositing -- but a frame feature cannot enumerate
	// them (it is generic, and an editor feature may not exist in a runtime
	// build). So the edge is declared INVERTED by each producer: a UI feature that
	// writes the present target declares BlockOn<FFrameRenderFeature, IPresent,
	// MyFinalStage> (reverse dep), and an absent producer is silently skipped by
	// the graph -- see FLayerTaskGraph::Init / BlockOn.
	//
	// No forward WaitFor here: the ordering is producer-driven so this feature
	// stays decoupled from any specific UI/editor feature's existence.
}

void FFrameRenderFeature::Present(FRender& R)
{
	const FRDGTextureRef Target = R.GetPresentTarget();
	if (Target.IsValid())
	{
		R.PresentTexture(Target);
	}
}

} // namespace Maho

// The C export FRender looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_FRAMERENDERFEATURE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FFrameRenderFeature::CreateLayer();
}
