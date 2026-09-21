#include "Scene.h"

#include "AssetTypes.h"
#include <Core/Profiler.h>
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

	// BeginRender is the FRAME HEAD and Present is the FRAME TAIL, both nodes of this collector's
	// graph -- so the frame boundary between them is a declared edge:
	//
	//     IBeginRender@N+1  waits for  IPresent@N
	//
	// It is what protects the RHI's single-depth frame state (one frame command buffer, one
	// in-flight fence, one acquired image): the next frame's head must not wait on / reset them
	// while this frame's tail is still submitting. Declarable HERE because both ends are stages of
	// the same graph; the host chain cannot express it (an edge from the engine graph into this one
	// is a silent no-op).
	//
	// The K-deep form -- RHI holding MAHO_FRAMES_IN_FLIGHT copies of that state, so frames may
	// genuinely overlap -- replaces this edge with a per-slot fence wait; until then this edge IS
	// the frame isolation, and it is why the host needs no per-frame Wait() any more.
	MyStage<IBeginRender>().WaitFor<FScene>().OnLastFrameStage<IPresent>();

	// Test producer of the draw protocol: hardcode the fullscreen triangle. No
	// vertex buffer -- the vertex shader generates its 3 positions from
	// gl_VertexIndex, so AddPass records it as Draw(VertexCount=3).
	FDrawBatch Triangle;
	Triangle.VertexCount = 3;
	TriangleDrawList.Add(Triangle);
}

void FScene::BeginRender(FRender& R, FRenderContext& Frame)
{
	MAHO_TRACE_SCOPE(nullptr, "open the swapchain frame head");
	// THE FRAME HEAD. Opening the swapchain frame is a frame primitive, so it happens in the frame's
	// first stage rather than on a host stage with an implied ordering: this node waits the previous
	// frame's tail (declared cross-frame edge in the ctor), the per-stage self edge keeps every later
	// stage within it, and everything that acquires a list or allocates from the pool declares an
	// edge onto this stage (see FUIFeature::IInitViews / DrawTriangleFeature / ExampleEditor).
	R.BeginSwapchainFrame();

	// Targets are (re)built on (re)size -- read AFTER the frame opened, so a swapchain recreation
	// inside it is already reflected in the canvas size. The clear is recorded + submitted in
	// Render; the feature no longer owns a command list here.
	EnsureTargets(R);
}

FScene::~FScene()
{
	GScene = nullptr;
}

void FScene::PreUnInstall(FRender& R, FRenderContext& Frame)
{
	MAHO_TRACE_SCOPE(nullptr, "release scene targets before unload");
	(void)R;
	// Drop the shared targets while OUR module is still loaded: the resource system
	// outlives this sub-plugin, so a leftover here would be destroyed (dtor / mirror
	// release) after this DLL is gone.
	if (Resource::FResourceSystem* RS = Resource::GetResourceSystem())
	{
		if (SceneColor.IsValid() || SceneDepth.IsValid())
		{
			RS->DestroyResource(kSceneColorName);
			RS->DestroyResource(kSceneDepthName);
		}
	}
	SceneColor = {};
	SceneDepth = {};
	SceneColorLayout = ESceneColorLayout::Undefined;
	CachedWidth = 0;
	CachedHeight = 0;
}

void FScene::EnsureTargets(FRender& R)
{
	MAHO_TRACE_SCOPE(nullptr, "ensure scene color and depth targets");
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
	MAHO_TRACE_SCOPE(nullptr, "rebuild targets on resize");
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
	// SceneColor was just (re)created: its native is born VK_IMAGE_LAYOUT_UNDEFINED,
	// but SceneColorLayout still holds the PREVIOUS target's value (RenderTarget after
	// a normal frame). If it is not reset here, the reactive Common -> RenderTarget
	// transition in Render() is skipped and every frame afterwards renders into an
	// image the validator still tracks as UNDEFINED (VUID-vkCmdBeginRendering-
	// pRenderingInfo-09592), and the downstream sampling flip chain desyncs. Depth is
	// already handled by bTargetsNeedTransition; color needs its own flag reset.
	SceneColorLayout = ESceneColorLayout::Undefined;

	CachedWidth = W;
	CachedHeight = H;
}

void FScene::Render(FRender& R, FRenderContext& Frame)
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

void FScene::EndRender(FRender& R, FRenderContext& Frame)
{
	// The clear is submitted at the end of Render (AddPass submits at its call
	// site), so this stage is now a no-op. It stays in the stage list so the draw
	// features' `WaitFor ... Scene::IEndRender` deps keep the same ordering -- their
	// IRender still runs after this stage, i.e. after the clear's submit above.
	(void)R;
}

void FScene::Present(FRender& R, FRenderContext& Frame)
{
	MAHO_TRACE_SCOPE(nullptr, "present the scene color");

	// The frame's ONE submission point: every pass recorded this frame, in the order the stage
	// nodes registered them (that IS the declared edge order), plus anything recorded off-frame.
	// This is where the "record now, submit once" model pays off -- the queue sees the whole frame
	// as one ordered batch instead of a submit per pass.
	R.SubmitRecordedPasses(Frame);

	// Then the blit. Recording it here (rather than from the host's IEndFrame, where it used to
	// live) is what turns "the present target's last writer runs first" into a declared edge: this
	// stage is the last in the sequence, so the in-frame chain already orders it after every
	// writer. Last writer wins -- a UI feature published the target in its own stage.
	if (const FRDGTextureRef Target = R.GetPresentTarget(); Target.IsValid())
	{
		R.PresentTexture(Target);
	}

	// THE FRAME END, right after the blit went into the frame command list: close it, submit it
	// (queue-order: behind the pass batch submitted above) and present. Hosting it here is what
	// removes the host's per-frame Wait() -- "recording finished before the close" is statement
	// order now, while the cross-frame edge in the ctor is what keeps the next frame's head off
	// this frame's opened swapchain state (one frame buffer, one fence, one acquired image).
	R.EndSwapchainFrame();
}

void FScene::TransitionSceneColorForSampling(FRender& R)
{
	MAHO_TRACE_SCOPE(nullptr, "transition scene color for sampling");
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
	MAHO_TRACE_SCOPE(nullptr, "transition scene color back to rendering");
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
extern "C" MAHO_SCENE_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::Scene::FScene::CreateFrame();
}
