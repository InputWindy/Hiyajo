#include "Scene.h"

#include <Frame.h>
#include <ImGuiSystem.h>
#include <Log.h>
#include <RHI/RHICommandList.h>
#include <RHI/RHIEnums.h>

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

	// Receive the ImGui draw data each frame (the ImGui host pushes it via this
	// sink -- a callback, so the engine ImGui plugin does not link this feature).
	SetImGuiDrawDataSink([](void* DrawData) {
		if (FScene* S = GScene)
		{
			S->SetImGuiDrawData(DrawData);
		}
	});
}

void FScene::BeginRender(FRender& R)
{
	// Acquire this feature's command list; Render records into it, EndRender submits.
	RenderList = R.AcquireRenderList();
	EnsureTargets(R);
}

void FScene::EnsureTargets(FRender& R)
{
	IRHI* RHIPtr = R.GetRHI();
	if (RHIPtr == nullptr)
	{
		return;
	}
	const std::uint32_t W = RHIPtr->GetFramebufferWidth();
	const std::uint32_t H = RHIPtr->GetFramebufferHeight();
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
	ColorDesc.Format = RHIPtr->GetSwapchainFormat();
	ColorDesc.Dimension = ERHITextureDimension::Tex2D;
	ColorDesc.Extent = { W, H, 1 };
	ColorDesc.MipLevels = 1;
	ColorDesc.ArrayLayers = 1;
	ColorDesc.Usage = ERHITextureUsage::ColorAttachment | ERHITextureUsage::Sampled | ERHITextureUsage::TransferSrc;
	ColorDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	SceneColor = R.CreateTexture(ColorDesc, /*bTransient=*/false);

	FRHITextureDesc DepthDesc;
	DepthDesc.Format = ERHIFormat::D32_SFLOAT;
	DepthDesc.Dimension = ERHITextureDimension::Tex2D;
	DepthDesc.Extent = { W, H, 1 };
	DepthDesc.MipLevels = 1;
	DepthDesc.ArrayLayers = 1;
	DepthDesc.Usage = ERHITextureUsage::DepthStencil;
	DepthDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	SceneDepth = R.CreateTexture(DepthDesc, /*bTransient=*/false);

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
	// Scene pass head: record into OUR OWN command list. Fresh targets get their
	// initial layout transition at the start; then the clear. Scene render features
	// (DrawTriangle, ...) draw into the same targets afterwards with LoadOp Load.
	if (RenderList == nullptr || !SceneColor.IsValid())
	{
		return;
	}
	IRHI* RHIPtr = R.GetRHI();
	if (RHIPtr == nullptr)
	{
		return;
	}
	FRHICommandList* Cmd = RenderList;
	Cmd->Begin();
	if (bTargetsNeedTransition)
	{
		bTargetsNeedTransition = false;
		Cmd->TransitionTexture(SceneColor.GetRHI(), ERHIResourceState::Common, ERHIResourceState::RenderTarget);
		Cmd->TransitionTexture(SceneDepth.GetRHI(), ERHIResourceState::Common, ERHIResourceState::DepthWrite);
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

	Cmd->BeginRendering(&Color, 1, PDepth, RHIPtr->GetFramebufferWidth(), RHIPtr->GetFramebufferHeight());
	Cmd->EndRendering();
	Cmd->End();
}

void FScene::EndRender(FRender& R)
{
	// Submit this feature's recorded command list. The graph deps order this after
	// every draw that targets the scene (DrawTriangle.EndRender depends on my
	// EndRender), so the clear is submitted before the draw.
	if (RenderList != nullptr)
	{
		if (IRHI* RHIPtr = R.GetRHI())
		{
			RHIPtr->Submit(RenderList, ERHICommandListType::Graphics);
		}
		RenderList = nullptr;
	}
}

} // namespace Scene
} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_SCENE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Scene::FScene::CreateLayer();
}
