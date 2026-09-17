#include "FrameRenderFeature.h"

namespace Maho
{

FFrameRenderFeature::FFrameRenderFeature()
{
	// The present point is the LAST frame stage. It must run only after every
	// render feature finished compositing -- but a frame feature cannot enumerate
	// them (it is generic, and an editor feature may not exist in a runtime
	// build). So the edge is declared INVERTED by each producer: a UI feature that
	// writes the present target declares IsBlocking<FFrameRenderFeature>().OnStage<IPresent>()
	// (reverse dep), and a producer that is not installed simply never makes the edge exist.
	//
	// No forward wait here: the ordering is producer-driven so this feature
	// stays decoupled from any specific UI/editor feature's existence.
}

void FFrameRenderFeature::Present(FRender& R)
{
	// ORDERING ANCHOR ONLY -- the present primitive is issued by FRender::EndFrame.
	//
	// This stage used to also call R.PresentTexture(), which conflated two different
	// jobs: deciding WHICH target to present (an ordering decision inside the render
	// sequence -- this is the stage producers attach their reverse edges to) and
	// ISSUING the present primitive (a frame-boundary operation that must stay serial
	// with BeginFrame/EndFrame).
	//
	// The second half is what broke: this stage lives in FRender's COLLECTOR graph,
	// while FRender::IBeginFrame / IEndFrame are nodes in the HOST graph. Two graphs,
	// and the engine has no cross-graph dependency edges -- so nothing could order them,
	// even though RHI.cpp:134 states that the caller must keep the frame primitives
	// serial. Moving the call into EndFrame puts all three primitives on one chain,
	// where the per-layer gate already serializes them across frames.
	//
	// "Last writer wins" stays deterministic: EndFrame reads the target AFTER the render
	// graph has been drained.
	(void)R;
}

} // namespace Maho

// The C export FRender looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_FRAMERENDERFEATURE_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FFrameRenderFeature::CreateFrame();
}
