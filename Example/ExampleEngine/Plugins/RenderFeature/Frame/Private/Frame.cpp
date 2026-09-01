#include "Frame.h"

#include <DrawTriangleFeature.h>
#include <ImGuiRender.h>
#include <RHI/RHIServer.h>
#include <Scene.h>

namespace Maho
{

FFrame::FFrame()
{
	// IPresent (the blit) must run after every render feature's IEndRender (their
	// submits), so the swapchain blit copies the fully-rendered scene. Hand-wired
	// for now -- no automatic "last stage per feature" wiring yet.
	WaitFor<IPresent, Scene::FScene, IEndRender>();
	WaitFor<IPresent, FDrawTriangleFeature, IEndRender>();
	WaitFor<IPresent, FImGuiRenderFeature, IRenderUI>();   // present blit carries the UI
}

void FFrame::Present(FRender& R)
{
	Scene::FScene* Scene = Scene::GetScene();
	if (Scene != nullptr)
	{
		FRDGTextureRef Color = Scene->GetSceneColor();
		if (Color.IsValid())
		{
			// Blit the scene color to the swapchain backbuffer -- the frame
			// feature owns the present point, FRender just exposes the RHI.
			if (IRHI* RHIPtr = R.GetRHI())
			{
				RHIPtr->PresentTexture(Color.GetRHI());
			}
		}
	}
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_FRAME_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FFrame::CreateLayer();
}
