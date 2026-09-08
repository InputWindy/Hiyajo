#include "Scene.h"

#include "AssetTypes.h"
#include <Log.h>
#include <Name.h>
#include <Resource.h>
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

// SceneColor/SceneDepth: two hardcoded persistent render targets. Created through the
// resource system (CreateResource) so they flow into GpuMirrors; FScene keeps the
// FRDGTextureRef resolved from the mirror (FRender built it with the matching RHI usage).
namespace
{
	constexpr std::string_view kSceneColorName = "SceneColor";
	constexpr std::string_view kSceneDepthName = "SceneDepth";
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

FScene::~FScene()
{
	GScene = nullptr;
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

	// Resize or first creation: drop the old resource-system entries (each DestroyResource
	// broadcasts OnAssetUnloaded -> the render mirror releases + erases the old target).
	Resource::FResourceSystem* RS = Resource::GetResourceSystem();
	if (RS != nullptr && (SceneColor.IsValid() || SceneDepth.IsValid()))
	{
		RS->DestroyResource(kSceneColorName);
		RS->DestroyResource(kSceneDepthName);
	}

	if (RS != nullptr)
	{
		// SceneColor: mirror the swapchain's backbuffer format so the present blit is
		// consistent with the backbuffer (an offscreen RT may differ, but keep it simple).
		const ERHIFormat SwFmt = R.GetSwapchainFormat();
		Resource::ETexturePixelFormat ColorFmt = Resource::ETexturePixelFormat::RGBA8;
		bool bSRGB = false;
		switch (SwFmt)
		{
			case ERHIFormat::R8G8B8A8_SRGB:  ColorFmt = Resource::ETexturePixelFormat::RGBA8; bSRGB = true; break;
			case ERHIFormat::R8G8B8A8_UNORM: ColorFmt = Resource::ETexturePixelFormat::RGBA8; bSRGB = false; break;
			default:
				MAHO_LOG_CORE_WARN("FScene: unsupported swapchain format for scene color; using RGBA8_UNORM");
				ColorFmt = Resource::ETexturePixelFormat::RGBA8;
				bSRGB = false;
				break;
		}

		Resource::TResourceCreateDesc<Resource::FTexture2D>::FConfig ColorCfg;
		ColorCfg.Format = ColorFmt;
		ColorCfg.Width = W;
		ColorCfg.Height = H;
		ColorCfg.ArrayLayers = 1;
		ColorCfg.MipCount = 1;
		ColorCfg.bSRGB = bSRGB;
		ColorCfg.Usage = Resource::ETextureMirrorUsage::ColorTarget;
		RS->CreateResource<Resource::FTexture2D>(kSceneColorName, ColorCfg);

		Resource::TResourceCreateDesc<Resource::FTexture2D>::FConfig DepthCfg;
		DepthCfg.Format = Resource::ETexturePixelFormat::D32Sfloat;
		DepthCfg.Width = W;
		DepthCfg.Height = H;
		DepthCfg.ArrayLayers = 1;
		DepthCfg.MipCount = 1;
		DepthCfg.Usage = Resource::ETextureMirrorUsage::DepthTarget;
		RS->CreateResource<Resource::FTexture2D>(kSceneDepthName, DepthCfg);

		// Resolve the Persistent mirrors FRender committed (CreateResource broadcasts
		// OnAssetCreated synchronously, so the mirrors are resident by now).
		if (const FRDGResourceRef* C = R.GetMirror(Name::FName(kSceneColorName)))
		{
			if (const FRDGTextureRef* T = std::get_if<FRDGTextureRef>(C)) { SceneColor = *T; }
		}
		if (const FRDGResourceRef* D = R.GetMirror(Name::FName(kSceneDepthName)))
		{
			if (const FRDGTextureRef* T = std::get_if<FRDGTextureRef>(D)) { SceneDepth = *T; }
		}
	}

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
		// SceneColor is both a render target (this clear / downstream draws) and, in an
		// editor build, a sampled mirror (the viewport reads it as SHADER_READ_ONLY). The
		// two uses need opposite layouts, so the layout is toggled each frame: an editor
		// UI compose pass flips it to SHADER_READ_ONLY (via TransitionSceneColorForSampling)
		// and back; here we flip it back to COLOR_ATTACHMENT before any scene write. A
		// fresh target (never transitioned) is Common/UNDEFINED and must be brought up once.
		if (SceneColor.GetRHI() != nullptr)
		{
			if (SceneColorLayout == ESceneColorLayout::ShaderResource)
			{
				Cmd.TransitionTexture(SceneColor.GetRHI(), ERHIResourceState::ShaderResource, ERHIResourceState::RenderTarget);
			}
			else if (SceneColorLayout == ESceneColorLayout::Undefined)
			{
				Cmd.TransitionTexture(SceneColor.GetRHI(), ERHIResourceState::Common, ERHIResourceState::RenderTarget);
			}
			// RenderTarget: already there, no-op.
			SceneColorLayout = ESceneColorLayout::RenderTarget;
		}

		if (bTargetsNeedTransition && SceneDepth.GetRHI() != nullptr)
		{
			bTargetsNeedTransition = false;
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

void FScene::TransitionSceneColorForSampling(FRender& R)
{
	// SceneColor leaves RenderTarget -> ShaderResource so a later sampled use (the
	// editor viewport mirror) binds it legally. AddPass submits at this call site, so
	// the transition is queued on the graphics queue BEFORE the UI compose pass that
	// samples it -- same queue, ordered submit, validation-layer layout tracking agrees.
	if (SceneColor.IsValid() && SceneColorLayout == ESceneColorLayout::RenderTarget)
	{
		FRHITexture* Tex = SceneColor.GetRHI();
		R.AddPass(ERHICommandListType::Graphics, [Tex](FRHICommandList& Cmd)
		{
			Cmd.TransitionTexture(Tex, ERHIResourceState::RenderTarget, ERHIResourceState::ShaderResource);
		});
		SceneColorLayout = ESceneColorLayout::ShaderResource;
	}
}

void FScene::TransitionSceneColorForRendering(FRender& R)
{
	// Undo the sampling flip so the next scene write can target COLOR_ATTACHMENT again.
	if (SceneColor.IsValid() && SceneColorLayout == ESceneColorLayout::ShaderResource)
	{
		FRHITexture* Tex = SceneColor.GetRHI();
		R.AddPass(ERHICommandListType::Graphics, [Tex](FRHICommandList& Cmd)
		{
			Cmd.TransitionTexture(Tex, ERHIResourceState::ShaderResource, ERHIResourceState::RenderTarget);
		});
		SceneColorLayout = ESceneColorLayout::RenderTarget;
	}
}

} // namespace Scene
} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_SCENE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Scene::FScene::CreateLayer();
}
