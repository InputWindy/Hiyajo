#include "Scene.h"

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
}

void FScene::BeginRender(FRender& R)
{
	EnsureTargets(R);
}

void FScene::EnsureTargets(FRender& R)
{
	const std::uint32_t W = R.GetFramebufferWidth();
	const std::uint32_t H = R.GetFramebufferHeight();
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
	// first BeginRendering; transition the fresh (UNDEFINED) images now.
	if (FRHICommandList* Cmd = R.GetFrameCommandList())
	{
		Cmd->TransitionTexture(SceneColor.GetRHI(), ERHIResourceState::Common, ERHIResourceState::RenderTarget);
		Cmd->TransitionTexture(SceneDepth.GetRHI(), ERHIResourceState::Common, ERHIResourceState::DepthWrite);
	}

	CachedWidth = W;
	CachedHeight = H;
}

void FScene::Render(FRender& R)
{
	// Scene pass head: clear the shared color + depth targets. Scene render
	// features (DrawTriangle, ...) then draw into them with LoadOp Load.
	FRHICommandList* Cmd = R.GetFrameCommandList();
	if (Cmd == nullptr || !SceneColor.IsValid())
	{
		return;
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

	Cmd->BeginRendering(&Color, 1, PDepth, R.GetFramebufferWidth(), R.GetFramebufferHeight());
	Cmd->EndRendering();
}

void FScene::EndRender(FRender&)
{
}

void FScene::Present(FRender& R)
{
	// The global scene feature is the present point: blit SceneColor to the
	// swapchain backbuffer at the end of the frame.
	R.Present(SceneColor);
}

} // namespace Scene
} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_SCENE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Scene::FScene::CreateLayer();
}
