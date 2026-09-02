#include "Scene.h"

#include <Frame.h>
#include <Log.h>
#include <RHI/RHICommandList.h>
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>

namespace Maho
{
namespace Scene
{

static FScene* GScene = nullptr;

FScene* GetScene()
{
	return GScene;
}

FScene::FScene()
{
	GScene = this;

	// BeginRender runs after the host FRender::BeginFrame (engine stage), which
	// already waited the previous fence, recycled the previous frame's lists and
	// began the frame buffer -- so list acquisition here cannot race the recycle.

	// Test producer of the draw protocol: hardcode the fullscreen triangle. No
	// vertex buffer -- the vertex shader generates its 3 positions from
	// gl_VertexIndex, so AddPass records it as Draw(VertexCount=3).
	FDrawBatch Triangle;
	Triangle.VertexCount = 3;
	TriangleDrawList.Add(Triangle);
}

void FScene::BeginRender(FRender& R)
{
	// Targets are (re)built on (re)size; the clear is recorded + submitted in
	// Render via AddPass. The feature no longer owns a command list here.
	EnsureTargets(R);
}

void FScene::EnsureTargets(FRender& R)
{
	const std::uint32_t W = R.GetCanvasWidth();
	const std::uint32_t H = R.GetCanvasHeight();
	if (W == 0 || H == 0)
	{
		return;
	}
	if (SceneColor.IsValid() && SceneDepth.IsValid() && W == CachedWidth && H == CachedHeight)
	{
		return;
	}

	// Resize or first creation: drop old targets and recreate.
	if (SceneColor.IsValid())
	{
		R.ReleaseTexture(SceneColor);
	}
	if (SceneDepth.IsValid())
	{
		R.ReleaseTexture(SceneDepth);
	}

	FRHITextureDesc ColorDesc;
	ColorDesc.Format = R.GetSwapchainFormat();
	ColorDesc.Dimension = ERHITextureDimension::Tex2D;
	ColorDesc.Extent = { W, H, 1 };
	ColorDesc.MipLevels = 1;
	ColorDesc.ArrayLayers = 1;
	ColorDesc.Usage = ERHITextureUsage::ColorAttachment | ERHITextureUsage::Sampled | ERHITextureUsage::TransferSrc;
	ColorDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	SceneColor = R.CreateTexture(ColorDesc, ERDGResourceLifetime::Persistent);

	FRHITextureDesc DepthDesc;
	DepthDesc.Format = ERHIFormat::D32_SFLOAT;
	DepthDesc.Dimension = ERHITextureDimension::Tex2D;
	DepthDesc.Extent = { W, H, 1 };
	DepthDesc.MipLevels = 1;
	DepthDesc.ArrayLayers = 1;
	DepthDesc.Usage = ERHITextureUsage::DepthStencil;
	DepthDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	SceneDepth = R.CreateTexture(DepthDesc, ERDGResourceLifetime::Persistent);

	// Dynamic rendering needs the attachments in the correct layout before the
	// first BeginRendering. The transition is recorded at the START of this
	// feature's own command list (the first list to use the targets), not the
	// frame buffer -- see Render().
	bTargetsNeedTransition = true;

	CachedWidth = W;
	CachedHeight = H;
}

void FScene::Render(FRender& R)
{
	// Scene pass head: RECORD + SUBMIT the clear in one AddPass (AddPass acquires
	// the list, Begin/End it, and submits at this call site -- so the clear runs in
	// the IRender stage). Draw features target the scene after me: their IRender is
	// ordered after my IEndRender (stage deps), which is after this submit, so the
	// clear reaches the queue before every draw. Fresh targets get their initial
	// layout transition at the start.
	if (!SceneColor.IsValid())
	{
		return;
	}
	R.AddPass(ERHICommandListType::Graphics, [&](FRHICommandList& Cmd)
	{
		if (bTargetsNeedTransition)
		{
			bTargetsNeedTransition = false;
			Cmd.TransitionTexture(SceneColor.GetRHI(), ERHIResourceState::Common, ERHIResourceState::RenderTarget);
			Cmd.TransitionTexture(SceneDepth.GetRHI(), ERHIResourceState::Common, ERHIResourceState::DepthWrite);
		}

		FRHIRenderingAttachmentInfo Color;
		Color.View = SceneColor.GetView();
		Color.LoadOp = ERHILoadOp::Clear;
		Color.StoreOp = ERHIStoreOp::Store;
		Color.ClearColor[0] = 0.15f;
		Color.ClearColor[1] = 0.25f;
		Color.ClearColor[2] = 0.45f;
		Color.ClearColor[3] = 1.0f;

		FRHIRenderingAttachmentInfo Depth;
		const FRHIRenderingAttachmentInfo* PDepth = nullptr;
		if (SceneDepth.IsValid())
		{
			Depth.View = SceneDepth.GetView();
			Depth.LoadOp = ERHILoadOp::Clear;
			Depth.StoreOp = ERHIStoreOp::Store;
			Depth.ClearColor[0] = 1.0f;   // depth clear
			PDepth = &Depth;
		}

		Cmd.BeginRendering(&Color, 1, PDepth, R.GetCanvasWidth(), R.GetCanvasHeight());
		Cmd.EndRendering();
	});
}

void FScene::EndRender(FRender& R)
{
	// The clear is submitted at the end of Render (AddPass submits at its call
	// site), so this stage is now a no-op. It stays in the stage list so the draw
	// features' `WaitFor ... Scene::IEndRender` deps keep the same ordering -- their
	// IRender still runs after this stage, i.e. after the clear's submit above.
	(void)R;
}

} // namespace Scene
} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_SCENE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Scene::FScene::CreateLayer();
}
