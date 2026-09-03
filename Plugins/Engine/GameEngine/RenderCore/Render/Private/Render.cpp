#include "Render.h"

#include <DrawTriangleFeature.h>
#include <Frame.h>
#include <UIFeature.h>
#include <Log.h>
#include <Name.h>
#include <Platform.h>
#include <Paths.h>
#include <Scene.h>
#include "RenderResourcePool.h"
#include "ShaderCompiler.h"

// Dear ImGui — compiled INTO this DLL (Render.cmake); the UI's CPU-side context
// is created/driven here, the render backend lives in UIFeature.
#include "imgui.h"

#include <algorithm>

#if defined(_WIN32)
#	include <windows.h>
#endif

namespace Maho
{

FRender* GRender = nullptr;
MAHO_RENDER_API FRender* GetRender()
{
	return GRender;
}

FRender::FRender()
{
	// Init: I read the window from Platform (its PostInitialize) and log heavily
	// (Log must be up first) -- both declared by ME, the consumer.
	MyStage<IInit>().IsWaiting<Platform::FPlatform>().ForStage<IPostInit>();
	MyStage<IInit>().IsWaiting<FLog>().ForStage<IInit>();
	// My PostInit imports assets (FPaths::Resolve) and interns names (FNamePool)
	// on the same stage - both must be post-init before I read them.
	MyStage<IPostInit>().IsWaiting<Paths::FPaths>().ForStage<IPostInit>();
	MyStage<IPostInit>().IsWaiting<Name::FNamePool>().ForStage<IPostInit>();

	// Shutdown: my teardown drains render tasks that may log, and I hold the RHI
	// surface created from Platform's window -- so Log and Platform must run
	// their Shutdown AFTER mine. Declared here (I know them), not by them.
	MyStage<IShutdown>().IsBlocking<FLog>().OnStage<IShutdown>();
	MyStage<IShutdown>().IsBlocking<Platform::FPlatform>().OnStage<IShutdown>();

	// Input: Platform's Tick polls GLFW first, then the ImGui host feeds io and
	// builds the UI (same frame). FRender owns the ImGui context directly.
	MyStage<ITick>().IsWaiting<Platform::FPlatform>().ForStage<ITick>();

	// Asset mirror: the render mirror consumes imported assets (upload to GPU), so
	// the resource system must run its IInit (start the IO thread) before mine.
	MyStage<IInit>().IsWaiting<Resource::FResourceSystem>().ForStage<IInit>();
}

FRender::~FRender() = default;

void FRender::PreInitialize(FEngineBase&)
{
}

void FRender::Initialize(FEngineBase& Engine)
{
	(void)Engine;
	GRender = this;

	// Create the render server (RHI) with the native window from the Platform.
	Platform::FPlatform* P = Platform::GetPlatform();
	if (P != nullptr && P->GetNativeWindow() != nullptr)
	{
		RHI = std::make_unique<FRHI>();
		if (!RHI->Initialize(P->GetNativeWindow(), P->GetWindowWidth(), P->GetWindowHeight()))
		{
			MAHO_LOG_CORE_ERROR("FRender::Initialize: RHI initialization failed");
			RHI.reset();
		}
	}

	// RDG resource pool (off-screen textures/buffers, cross-frame reuse).
	ResourcePool = std::make_unique<FRenderResourcePool>(RHI.get());

	// Async shader compiler (dedicated compile thread).
	ShaderCompiler = std::make_unique<FShaderCompilerServer>();
	ShaderCompiler->Initialize();

	// Persistent render graph: Flush at frame start, Execute at frame end.
	RenderGraph = std::make_unique<FLayerTaskGraph<FRenderStages, FRender>>(Pool, *this);

	// Install the global scene feature + the render features into OUR layer
	// collection (not the host engine's) so the render graph drives them.
	// All are engine plugins beside this one (same layer, Plugins/Engine/).
	// They are loaded by DLL name at runtime (Install<T> → FAssembly), so this
	// DLL only includes their headers and never links them -- see Render.cplugin
	// PrivateIncludes. Frame drives the swapchain begin/end as a scheduled stage
	// and must load last (its IPresent depends on the other features' IEndRender).
	Install<Scene::FScene>();
	Install<FDrawTriangleFeature>();
	Install<FFrame>();
	Install<FUIFeature>();

	// ImGui context (CPU side) — created from the Platform layer's window; the
	// render backend (UIFeature, installed above) consumes the draw data this
	// context produces each frame. FRender owns the context lifetime.
	Platform::FPlatform* UI_P = Platform::GetPlatform();
	if (UI_P == nullptr || UI_P->GetWindowWidth() == 0 || UI_P->GetToolkitWindowHandle() == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FRender::Initialize: no window; UI disabled");
	}
	else
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& IO = ImGui::GetIO();
		IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // the editor shell docks later
		ImGui::StyleColorsDark();
		bUIInitialized = true;
		MAHO_LOG_CORE_INFO("FRender: ImGui context created (CPU side; render backend in UIFeature)");
	}

	// CPU asset -> GPU mirror: listen for imported assets (create the GPU mirror +
	// upload its bulk), for unloaded assets (release the mirror), and provide the
	// GPU fill-back for export. Bound here so the asset system (IInit before mine)
	// is up; unbind in Shutdown.
	if (Resource::FResourceSystem* RS = Resource::GetResourceSystem())
	{
		RS->OnAssetImported.Bind([this](const Name::FName& N, Resource::FOnTransferDone D)
		{
			OnAssetMirrorImported(N, std::move(D));
		});
		RS->OnAssetUnloaded.Bind([this](const Name::FName& N, Resource::FOnTransferDone D)
		{
			OnAssetMirrorUnloaded(N, std::move(D));
		});
		RS->SetReadback([this](const Name::FName& N, Resource::FResource& R)
		{
			return ReadbackMirror(N, R);
		});
	}
	else
	{
		MAHO_LOG_CORE_WARN("FRender: resource system unavailable; asset mirror disabled");
	}
}

void FRender::PostInitialize(FEngineBase&)
{
	// Test harness: import a CPU texture asset right after Resource's IInit, so the
	// async mirror OnAssetImported fires and the GPU mirror is built, then shown by
	// the ImGui image below (DisplayMirrorImGui in Tick). Absolute path is passed
	// through by FPaths::Resolve (no virtual-root alias).
	if (Resource::FResourceSystem* RS = Resource::GetResourceSystem())
	{
		if (RS->Import<Resource::FTexture2D>({ "D:/TestPackage/test.png" }))
		{
			MAHO_LOG_CORE_INFO("FRender: queued texture import D:/TestPackage/test.png");
		}
		else
		{
			MAHO_LOG_CORE_WARN("FRender: import D:/TestPackage/test.png failed to queue");
		}
	}
	else
	{
		MAHO_LOG_CORE_WARN("FRender: no resource system; test import skipped");
	}
}

void FRender::WaitShaderCompiles()
{
	if (ShaderCompiler)
	{
		// The shader server is a quiescence barrier: FlushCompiles waits until every
		// CompileAsync submitted so far has completed. Calling this from a handle's
		// Wait() is the "sync before use" point -- after it returns, the per-T state
		// holds the compiled bytecode (and bReady).
		ShaderCompiler->FlushCompiles();
	}
}

void FRender::PreShutdown(FEngineBase&)
{
}

void FRender::Shutdown(FEngineBase&)
{
	// Clean-exit guarantee: drain EVERY async worker BEFORE tearing anything
	// down. The engine's shutdown graph runs the IShutdown stages concurrently,
	// so this layer must be quiescent before we touch shared state -- the render
	// feature graph pool first (a task still in flight holds the graph pointer),
	// then the two threaded servers (shader-compile thread + RHI render-server
	GRender = nullptr;

	// Unbind the asset-mirror delegates. The resource system may be shutting down
	// concurrently; GetResourceSystem() returns null then (Resource's own Shutdown
	// clears it), so the delegates are dropped only while the system is alive. The
	// mirror table entries are Persistent RDG refs owned by the resource pool,
	// released by ResourcePool->Shutdown below -- just drop the refs.
	if (Resource::FResourceSystem* RS = Resource::GetResourceSystem())
	{
		RS->OnAssetImported.RemoveAll();
		RS->OnAssetUnloaded.RemoveAll();
	}
	GpuMirrors.clear();
	MirrorUISets.clear();   // descriptor sets are pool-owned; just drop the handles
	// thread). All of them drain their queues and join here.
	//
	// The leftover render tasks drained below (e.g. the last frame's EndFrame)
	// may still present; the swapchain surface is still alive here because
	// FPlatform's Shutdown is ordered AFTER ours (it owns the window/surface the
	// RHI was created from). The RHI stays a stateless task processor -- it
	// never refuses work, it just runs what it is given.
	if (RenderGraph)
	{
		RenderGraph->Flush();   // render feature graph pool (quiescence barrier)
	}
	if (ShaderCompiler)
	{
		ShaderCompiler->Shutdown();   // compile thread: drain pending compiles, stop + join
	}
	if (RHI)
	{
		RHI->Shutdown();   // render-server thread: drain + join (device teardown stays in ShutdownRHI)
	}
	RenderGraph.reset();

	// All render work for the last frame was submitted (the graph drives the
	// features synchronously); the GPU may still be executing it. Wait here so
	// every resource destroyed below is released only after the device is idle.
	if (RHI)
	{
		RHI->WaitIdle();

		// The last frame's feature command lists + pooled resources are destroyed
		// below by ResourcePool->Shutdown() while the device is still alive (their
		// VkCommandPools / VkBuffers must not be outstanding when the device dies).

		// Destroy the render feature instances explicitly BEFORE the RHI goes
		// away: their destructors free Vulkan objects (pipelines / shader
		// modules) that must be released while the device exists. The collector
		// would otherwise destroy them only when this FRender dies -- after the
		// RHI.
		Features.clear();
	}

	// The compile server thread was already joined above; just release the server.
	ShaderCompiler.reset();

	// Release pooled resources before the RHI device goes away.
	if (ResourcePool)
	{
		ResourcePool->Shutdown();
		ResourcePool.reset();
	}

	if (RHI)
	{
		RHI->ShutdownRHI();   // re-joining the server thread is a no-op; tears down the device
		RHI.reset();
	}

	// ImGui context (CPU-side; independent of the RHI device). Destroy after all
	// render teardown so no in-flight draw path touches it.
	if (bUIInitialized)
	{
		ImGui::DestroyContext();
		bUIInitialized = false;
	}
}

void FRender::PostShutdown(FEngineBase&)
{
}

void FRender::BeginFrame(FEngineBase&)
{
	// Swapchain frame lifecycle lives on the host (engine) stages, not the render
	// graph: RHI->BeginFrame waits the previous fence, acquires the swapchain
	// image and begins the frame buffer; then the previous frame's feature
	// command lists are recycled (their submits are done) and the resource pool
	// advances -- all before the render graph runs in Tick.
	if (IRHI* RHIp = RHI.get())
	{
		RHIp->BeginFrame();
	}
	BeginResourcePool();

	// ImGui frame feed -- display size + Win32 input + the (lazy) font-atlas build,
	// then NewFrame. CPU side only; the render backend uploads the atlas lazily.
	if (bUIInitialized)
	{
		ImGuiIO& IO = ImGui::GetIO();
		Platform::FPlatform* P = Platform::GetPlatform();
		if (P != nullptr)
		{
			// Display size must match the render target (SceneColor = swapchain
			// extent), not the window's logical size -- ImGui lays out in DisplaySize
			// coordinates and the render feature clips against it.
			IO.DisplaySize = ImVec2(
				static_cast<float>(P->GetWindowWidth()),
				static_cast<float>(P->GetWindowHeight()));
#if defined(_WIN32)
			// Input bypasses GLFW's message-driven cursor state (the window is created
			// on a pool worker and polled on another, so WM_MOUSEMOVE never reaches
			// glfwGetCursorPos). Win32 global state works from any thread.
			if (HWND Hwnd = static_cast<HWND>(P->GetNativeWindow()))
			{
				POINT Pt{};
				if (::GetCursorPos(&Pt) && ::ScreenToClient(Hwnd, &Pt))
				{
					RECT Client{};
					::GetClientRect(Hwnd, &Client);
					const float ScaleX = Client.right > 0 ? IO.DisplaySize.x / static_cast<float>(Client.right) : 1.f;
					const float ScaleY = Client.bottom > 0 ? IO.DisplaySize.y / static_cast<float>(Client.bottom) : 1.f;
					IO.AddMousePosEvent(static_cast<float>(Pt.x) * ScaleX, static_cast<float>(Pt.y) * ScaleY);
				}
				IO.AddMouseButtonEvent(0, (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
				IO.AddMouseButtonEvent(1, (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
				IO.AddMouseButtonEvent(2, (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
			}
#endif
		}

		// Renderer-backend NewFrame duty (mirrors the imgui_impl_* backends, which
		// must be called before ImGui::NewFrame()): the font atlas is built lazily by
		// ImFontAtlas::Build() and ImGui::NewFrame() asserts IsBuilt(). Calling
		// GetTexDataAsRGBA32() triggers that build on the first frame and returns the
		// existing pixels afterwards; the GPU upload happens later, in the UIFeature
		// backend's EnsureUIBackend, which calls it again and gets the same pixels.
		unsigned char* FontPixels = nullptr;
		int FontW = 0, FontH = 0, FontBpp = 0;
		IO.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH, &FontBpp);
		if (FontPixels == nullptr || FontW <= 0 || FontH <= 0)
		{
			MAHO_LOG_CORE_ERROR("FRender: font atlas not built");
		}
		else
		{
			ImGui::NewFrame();
		}
	}
}

void FRender::BeginResourcePool()
{
	if (ResourcePool)
	{
		ResourcePool->BeginFrame();
	}
}

void FRender::Tick(FEngineBase&)
{
	if (!RenderGraph)
	{
		return;
	}

	// Frame-start barrier: wait the PREVIOUS frame's render-graph tasks before
	// rebuilding the graph. The render CPU work runs during the interval and is
	// collected here, so Init never races with in-flight Render() calls.
	RenderGraph->Flush();

	// Build the ImGui frame (CPU side) between NewFrame (BeginFrame) and the
	// render-graph Execute below. UI construction is done by plugins directly on
	// the ImGui API (no wrapper layer): each plugin declares
	//   MyStage<ITick>().IsBlocking<Maho::FRender>().OnStage<ITick>();
	// so its ITick runs BEFORE this one -- it calls ImGui::Begin/Text/... and this
	// layer only closes the frame with a single ImGui::Render(). Plugins never call
	// NewFrame/Render/GetDrawData themselves. The collected draw data feeds the
	// UIFeature render backend (reads R.UIData) inside the graph below.
	if (bUIInitialized)
	{
		DisplayMirrorImGui();   // test harness: show the imported texture mirror
		ImGui::Render();
		UIData = ImGui::GetDrawData();
	}

	// Rebuild + drive the render feature graph. All frame work (swapchain begin,
	// feature acquire/record/submit, present, swapchain end) is a scheduled stage --
	// FRender only schedules; the frame feature + per-feature deps order it all.
	FlushPendingUpdatePipelines<IInitViews, IBeginRender, IRender, IEndRender, IPostProcess, IRenderUI, IPresent>();
	RenderGraph->Init(Select<IInitViews, IBeginRender, IRender, IEndRender, IPostProcess, IRenderUI, IPresent>());
	if (!RenderGraph->Compile())
	{
		ReportFatal("FRender::Tick: render pipeline Compile failed");
	}

	RenderGraph->Execute();
	// No trailing Flush: the render graph pipelines across frames -- the next
	// Tick's leading Flush (above) waits this frame's tasks. At shutdown the
	// leftover tasks are drained by FRender::Shutdown (render-pool Flush at its
	// start) and FLog's Shutdown is ordered after FRender's, so any teardown
	// logging lands in a live logger.
}

void FRender::EndFrame(FEngineBase&)
{
	// RHI->EndFrame (end + submit the frame buffer, present the swapchain) must
	// run after every feature submit, so drain the async render-graph tasks first.
	// This serializes the present behind this frame's draws (the graph is no
	// longer cross-frame pipelined across Tick -- a later auto-barrier pass can
	// restore the overlap).
	if (RenderGraph)
	{
		RenderGraph->Flush();
	}
	if (IRHI* RHIp = RHI.get())
	{
		RHIp->EndFrame();
	}
}

void FRender::RequestExit(FEngineBase&)
{
}

FRDGTextureRef FRender::CreateTexture(const FRHITextureDesc& Desc, ERDGResourceLifetime Lifetime)
{
	return ResourcePool ? ResourcePool->CreateTexture(Desc, Lifetime) : FRDGTextureRef{};
}

FRDGBufferRef FRender::CreateBuffer(const FRHIBufferDesc& Desc, ERDGResourceLifetime Lifetime)
{
	return ResourcePool ? ResourcePool->CreateBuffer(Desc, Lifetime) : FRDGBufferRef{};
}

void FRender::ReleaseTexture(FRDGTextureRef& Ref)
{
	if (ResourcePool)
	{
		ResourcePool->ReleaseTexture(Ref);
	}
}

void FRender::ReleaseBuffer(FRDGBufferRef& Ref)
{
	if (ResourcePool)
	{
		ResourcePool->ReleaseBuffer(Ref);
	}
}

void FRender::AddPass(ERHICommandListType PassType, std::function<void(FRHICommandList&)> PassFn)
{
	SubmitPass(PassType, std::move(PassFn));
}

void FRender::SubmitPass(ERHICommandListType PassType, std::function<void(FRHICommandList&)> Record)
{
	FRHICommandList* List = ResourcePool ? ResourcePool->AcquireRenderList() : nullptr;
	if (List == nullptr)
	{
		return;
	}
	List->Begin();
	Record(*List);
	List->End();
	if (IRHI* P = RHI.get())
	{
		P->Submit(List, PassType);
	}
}

FRHIShaderModule* FRender::GetOrCreateShaderModule(const FRHIShaderModuleDesc& Desc)
{
	return ResourcePool ? ResourcePool->GetOrCreateShaderModule(Desc) : nullptr;
}

FRHIPipelineLayout* FRender::GetOrCreatePipelineLayout(const FRHIPipelineLayoutDesc& Desc)
{
	return ResourcePool ? ResourcePool->GetOrCreatePipelineLayout(Desc) : nullptr;
}

FRHIDescriptorSetLayout* FRender::GetOrCreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc& Desc)
{
	return ResourcePool ? ResourcePool->GetOrCreateDescriptorSetLayout(Desc) : nullptr;
}

FRHIGraphicsPipeline* FRender::GetOrCreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc)
{
	return ResourcePool ? ResourcePool->GetOrCreateGraphicsPipeline(Desc) : nullptr;
}

FRHISampler* FRender::CreateSampler(const FRHISamplerDesc& Desc)
{
	return ResourcePool ? ResourcePool->CreateSampler(Desc) : nullptr;
}

FRHIDescriptorSet* FRender::CreateDescriptorSet(FRHIDescriptorSetLayout* Layout, const FRHIDescriptorSetLayoutDesc& LayoutDesc)
{
	return ResourcePool ? ResourcePool->CreateDescriptorSet(Layout, LayoutDesc) : nullptr;
}

std::uint32_t FRender::GetCanvasWidth() const
{
	return RHI ? RHI->GetFramebufferWidth() : 0;
}

std::uint32_t FRender::GetCanvasHeight() const
{
	return RHI ? RHI->GetFramebufferHeight() : 0;
}

ERHIFormat FRender::GetSwapchainFormat() const
{
	return RHI ? RHI->GetSwapchainFormat() : ERHIFormat::Unknown;
}

void FRender::PresentTexture(const FRDGTextureRef& Texture)
{
	if (RHI && ResourcePool)
	{
		RHI->PresentTexture(ResourcePool->GetTexture(Texture));
	}
}

// -- CPU asset -> GPU mirror --

ERHIFormat FRender::FormatMirror(Resource::ETexturePixelFormat Fmt, bool bSRGB)
{
	switch (Fmt)
	{
		case Resource::ETexturePixelFormat::RGBA8:
			return bSRGB ? ERHIFormat::R8G8B8A8_SRGB : ERHIFormat::R8G8B8A8_UNORM;
		case Resource::ETexturePixelFormat::RGBA16F:
			return ERHIFormat::R16G16B16A16_SFLOAT;
		case Resource::ETexturePixelFormat::RGBA32F:
			return ERHIFormat::R32G32B32A32_SFLOAT;
		case Resource::ETexturePixelFormat::R8:
			return ERHIFormat::R8_UNORM;
		case Resource::ETexturePixelFormat::RG8:
			return ERHIFormat::R8G8_UNORM;
		case Resource::ETexturePixelFormat::RGB8:
			return ERHIFormat::R8G8B8_UNORM;
		case Resource::ETexturePixelFormat::R16F:
			return ERHIFormat::R16_SFLOAT;
		// Block-compressed (BlockCompressed/DXT1/DXT5/BC7) and Unknown: no dedicated
		// RHI format yet, and a BC upload needs block-aligned rows. Returns Unknown
		// so UploadTextureMirror fails cleanly. TODO: map once the RHI grows the
		// BC1/BC3/BC7 formats (and the upload path handles block alignment).
		default:
			return ERHIFormat::Unknown;
	}
}

ERHITextureDimension FRender::DimensionMirror(Resource::ETextureDimension Dim)
{
	switch (Dim)
	{
		case Resource::ETextureDimension::Tex2D:
			return ERHITextureDimension::Tex2D;
		case Resource::ETextureDimension::Tex3D:
			return ERHITextureDimension::Tex3D;
		case Resource::ETextureDimension::TexCube:
			return ERHITextureDimension::Cube;
		case Resource::ETextureDimension::Tex2DArray:
		case Resource::ETextureDimension::TexCubeArray:
			return ERHITextureDimension::Tex2DArray;
		// 1D has no RHI dimension; fall back to 2D with height 1.
		default:
			return ERHITextureDimension::Tex2D;
	}
}

bool FRender::UploadTextureMirror(const Name::FName& AssetName, Resource::FTexture& Tex)
{
	const ERHIFormat Fmt = FormatMirror(Tex.GetPixelFormat(), Tex.IsSRGB());
	if (Fmt == ERHIFormat::Unknown)
	{
		MAHO_LOG_CORE_ERROR("FRender: unsupported texture pixel format for mirror");
		return false;
	}
	const std::vector<std::uint8_t>& Pixels = Tex.GetPixels();
	if (Pixels.empty())
	{
		return false;
	}

	FRHITextureDesc Desc;
	Desc.Format = Fmt;
	Desc.Dimension = DimensionMirror(Tex.GetDimension());
	Desc.Extent.Width = Tex.GetWidth();
	Desc.Extent.Height = Tex.GetHeight();
	Desc.Extent.Depth = Tex.GetDepth();
	Desc.MipLevels = Tex.GetMipCount();
	Desc.ArrayLayers = Tex.GetArrayLayers();
	Desc.Usage = ERHITextureUsage::Sampled | ERHITextureUsage::TransferDst;
	Desc.MemoryUsage = ERHIMemoryUsage::GPUOnly;

	FRDGTextureRef TexRef = CreateTexture(Desc, ERDGResourceLifetime::Persistent);
	if (!TexRef.IsValid() || TexRef.GetRHI() == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FRender: mirror texture allocation failed");
		return false;
	}

	FRHIBufferDesc Staging;
	Staging.Size = static_cast<std::uint64_t>(Pixels.size());
	Staging.Usage = ERHIBufferUsage::TransferSrc;
	Staging.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	FRDGBufferRef StagingRef = CreateBuffer(Staging, ERDGResourceLifetime::Transient);
	if (!StagingRef.IsValid() || StagingRef.GetRHI() == nullptr)
	{
		ReleaseTexture(TexRef);
		MAHO_LOG_CORE_ERROR("FRender: mirror staging buffer allocation failed");
		return false;
	}

	// Commit the mirror (Persistent texture) before the transfer submit so a later
	// unload / export sees it.
	GpuMirrors[AssetName] = TexRef;

	// Build the UI image handle (a set-0 CombinedImageSampler descriptor set shared
	// with the UI font layout) so ImGui::Image can display this mirror. Sampler is
	// pool-cached by descriptor (ClampToEdge, matching the UI font).
	FRHIDescriptorSet* UISet = CreateMirrorUIImage(AssetName, TexRef);
	FRHISamplerDesc MirrorSamplerDesc;
	MirrorSamplerDesc.AddressU = ERHIAddressMode::ClampToEdge;
	MirrorSamplerDesc.AddressV = ERHIAddressMode::ClampToEdge;
	MirrorSamplerDesc.AddressW = ERHIAddressMode::ClampToEdge;
	FRHISampler* MirrorSampler = CreateSampler(MirrorSamplerDesc);

	// One transfer submit outside a render pass: copy the CPU pixels into a staging
	// buffer then CopyBufferToTexture. UIFeature::UploadFont follows the same
	// pattern. Pixels are copied synchronously during record (still valid here --
	// Done() below is what drops the CPU bulk).
	FRHITexture* RHITex = TexRef.GetRHI();
	FRHIBuffer* RHIStaging = StagingRef.GetRHI();
	const std::uint8_t* PixelsData = Pixels.data();
	const std::uint64_t PixelBytes = static_cast<std::uint64_t>(Pixels.size());
	AddPass(ERHICommandListType::Graphics, [=](FRHICommandList& Cmd)
	{
		Cmd.UpdateBuffer(RHIStaging, 0, PixelBytes, PixelsData);
		Cmd.TransitionTexture(RHITex, ERHIResourceState::Common, ERHIResourceState::CopyDst);
		Cmd.CopyBufferToTexture(RHIStaging, RHITex, 0);
		Cmd.TransitionTexture(RHITex, ERHIResourceState::CopyDst, ERHIResourceState::ShaderResource);
		if (UISet != nullptr && MirrorSampler != nullptr)
		{
			FRHIDescriptorWrite MirrorWrite;
			MirrorWrite.Set = UISet;
			MirrorWrite.Binding = 0;
			MirrorWrite.Type = ERHIDescriptorType::CombinedImageSampler;
			MirrorWrite.TextureView = TexRef.GetView();
			MirrorWrite.Sampler = MirrorSampler;
			Cmd.UpdateDescriptorSets(&MirrorWrite, 1);
		}
	});
	return true;
}

FRHIDescriptorSet* FRender::CreateMirrorUIImage(const Name::FName& AssetName, const FRDGTextureRef& Tex)
{
	(void)Tex;
	// Same set-0 CombinedImageSampler + Fragment desc as the UI font layout, so the
	// get-or-create returns the shared layout and the bound set is accepted by the
	// UI pipeline's set-0 binding.
	FRHIDescriptorBinding B;
	B.Binding = 0;
	B.Type = ERHIDescriptorType::CombinedImageSampler;
	B.Count = 1;
	B.Stages = ERHIShaderStage::Fragment;
	FRHIDescriptorSetLayoutDesc Desc;
	Desc.Bindings.push_back(B);
	FRHIDescriptorSetLayout* Layout = GetOrCreateDescriptorSetLayout(Desc);
	if (Layout == nullptr)
	{
		return nullptr;
	}
	FRHIDescriptorSet* Set = CreateDescriptorSet(Layout, Desc);
	if (Set != nullptr)
	{
		MirrorUISets[AssetName] = Set;
	}
	return Set;
}

void FRender::DisplayMirrorImGui()
{
	for (const auto& [AssetName, UI] : MirrorUISets)
	{
		if (UI == nullptr)
		{
			continue;
		}
		const auto It = GpuMirrors.find(AssetName);
		if (It == GpuMirrors.end())
		{
			continue;
		}
		const FRDGTextureRef* Tex = std::get_if<FRDGTextureRef>(&It->second);
		if (Tex == nullptr)
		{
			continue;
		}

		const std::string Title = "texture mirror: " + std::string(AssetName.ToString());
		// The mirror window tracks the application frame size every frame, so it
		// grows/shrinks with the OS window. NoResize: its size is driven by the app
		// window, not a manual drag handle.
		const ImVec2& Disp = ImGui::GetIO().DisplaySize;
		ImGui::SetNextWindowSize(
			ImVec2(Disp.x * 0.9f, Disp.y * 0.9f),
			ImGuiCond_Always);
		if (ImGui::Begin(Title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
		{
			// Fit the image into the window content region, preserving aspect ratio, so
			// it scales up/down with the window (and thus the OS window). Reserve a
			// line below for the caption text.
			const ImVec2 Avail = ImGui::GetContentRegionAvail();
			const float IW = static_cast<float>(Tex->GetWidth());
			const float IH = static_cast<float>(Tex->GetHeight());
			constexpr float CaptionH = 20.0f;
			const float Scale = (std::min)(Avail.x / IW, (Avail.y - CaptionH) / IH);
			ImGui::Image(reinterpret_cast<ImTextureID>(UI), ImVec2(IW * Scale, IH * Scale));
			ImGui::Text("asset=%s  %ux%u", AssetName.ToString().data(), Tex->GetWidth(), Tex->GetHeight());
		}
		ImGui::End();
	}
}

void FRender::OnAssetMirrorImported(const Name::FName& AssetName, Resource::FOnTransferDone Done)
{
	bool bSuccess = false;
	if (Resource::FResourceSystem* RS = Resource::GetResourceSystem())
	{
		Resource::FResource* R = RS->FindMutable(AssetName.ToString());
		if (Resource::FTexture* Tex = dynamic_cast<Resource::FTexture*>(R))
		{
			bSuccess = UploadTextureMirror(AssetName, *Tex);
			MAHO_LOG_CORE_INFO("FRender: mirror imported asset={} ({}x{}, {}) => {}",
				AssetName.ToString(),
				Tex->GetWidth(), Tex->GetHeight(),
				Tex->IsSRGB() ? "sRGB" : "linear",
				bSuccess ? "OK" : "FAILED");
		}
		else
		{
			MAHO_LOG_CORE_WARN("FRender: mirror import asset={} (non-texture, skipped)",
				AssetName.ToString());
		}
	}
	if (Done)
	{
		Done(bSuccess, bSuccess ? std::string_view() : std::string_view("render mirror failed"));
	}
}

void FRender::OnAssetMirrorUnloaded(const Name::FName& AssetName, Resource::FOnTransferDone Done)
{
	const auto It = GpuMirrors.find(AssetName);
	if (It != GpuMirrors.end())
	{
		if (FRDGTextureRef* Tex = std::get_if<FRDGTextureRef>(&It->second))
		{
			ReleaseTexture(*Tex);
		}
		GpuMirrors.erase(It);
	}
	if (Done)
	{
		Done(true, std::string_view());
	}
}

bool FRender::ReadbackMirror(const Name::FName& /*AssetName*/, Resource::FResource& /*OutResource*/)
{
	// GPU -> CPU fill-back before an export. The RHI currently exposes no CPU
	// readback path to FRender (only CopyTextureToBuffer into a GPU buffer +
	// a private allocator Map), so this returns false: the mirror keeps the CPU
	// bulk dropped, and a pending export of a dropped resource is a known gap.
	// TODO: add an RHI readback API and decode the mirror here.
	return false;
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_RENDER_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FRender::CreateLayer();
}
