#include "Render.h"

#include <Core/Profiler.h>
#include <DrawTriangleFeature.h>
#include <UIFeature.h>
#include <Log.h>
#include <Name.h>
#include <Platform.h>
#include <Paths.h>
#include <Scene.h>
#include <GameWorld.h>
#include <RHI/RHIEnums.h>
#include "RenderResourcePool.h"
#include "ShaderCompiler.h"

#include <algorithm>
#include <typeindex>

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

	// The platform's ITick is what PUMPS the OS message queue (FPlatform::Tick -> PollEvents),
	// and that pump is what refreshes the input snapshot every consumer reads. With no edge, my
	// ITick -- and therefore every render stage inside it, including the UI features' input feed
	// (FUIFeature::InitViews, FExampleEditor::InitEditorViews) -- runs CONCURRENTLY with that
	// pump, so a given frame reads the snapshot from either before or after this frame's poll.
	// The ordering then jitters frame to frame: the cursor follows, then stalls, and an ImGui
	// window drag loses its grip because the button state was read a frame stale. Initialization
	// already declares this dependency (IInit waits for FPlatform::IPostInit); the per-frame path
	// needs it just as much.
	MyStage<ITick>().IsWaiting<Platform::FPlatform>().ForStage<ITick>();

	// Shutdown: my teardown drains render tasks that may log, and I hold the RHI
	// surface created from Platform's window -- so Log and Platform must run
	// their Shutdown AFTER mine. Declared here (I know them), not by them.
	MyStage<IShutdown>().IsBlocking<FLog>().OnStage<IShutdown>();
	MyStage<IShutdown>().IsBlocking<Platform::FPlatform>().OnStage<IShutdown>();
	// The mirror binds std::function targets rooted in THIS DLL into the resource
	// system's events (OnAsset* + SetReadback), and the UI feature subscribes to
	// GameWorld's UISystem. Both producers must Shutdown AFTER me so my teardown
	// RemoveAll/Unsubscribes against live objects -- never a nulled global accessor.
	// Resource is typed (already included); GameWorld typed too (header included
	// above) -- both producers must Shutdown AFTER me so my teardown runs against
	// live objects.
	MyStage<IShutdown>().IsBlocking<Resource::FResourceSystem>().OnStage<IShutdown>();
	MyStage<IShutdown>().IsBlocking<GameWorld::FGameWorld>().OnStage<IShutdown>();
	// The name pool is the same kind of environment, and it was the one missing: my teardown
	// path reaches FResourceSystem::DestroyResource, whose FIRST act is to intern a name
	// (Name::FName(AssetPath)). FNamePool::Shutdown retracts the pool, so without this edge it
	// runs BEFORE mine and the interning reads freed pool storage -- validated crash:
	// FScene::PreUnInstall -> DestroyResource -> FName ctor, inside my own IShutdown batch.
	// Declared here (I know it), not by them -- a producer never enumerates its consumers.
	MyStage<IShutdown>().IsBlocking<Name::FNamePool>().OnStage<IShutdown>();
	// The UI view registry is the same kind of environment: the editor panels this layer
	// tears down below (FExampleEditor::PreUnInstall -> try-uninstall -> each panel's
	// IEditorShutdown) unregister their views there, so the registry's IShutdown must run
	// AFTER mine. Declared by NAME, not by type: the UI plugin is optional for this layer
	// (no build dependency, and the graph skips the edge when it is not installed).
	MyStage<IShutdown>().IsBlocking("FUIViewRegistry").OnStage<IShutdown>();

	// Asset mirror: the render mirror consumes imported assets (upload to GPU), so
	// the resource system must run its IInit (start the IO thread) before mine.
	MyStage<IInit>().IsWaiting<Resource::FResourceSystem>().ForStage<IInit>();

	// Asset mirror at RUNTIME: the resource system's ITick drains its import queue on
	// its own thread and fans the result straight into my mirror
	// (OnAssetMirrorImported -> upload, OnAssetMirrorUnloaded -> release), which
	// allocates from MY resource pool and submits its own transfer pass. That drain
	// is invisible to my render graph, so it must be ordered against the frame:
	//   - after my IBeginFrame, which advances the pool and hands every transient slot
	//     of the finished frame back to the free list;
	//   - before my ITick, which dispatches the graph whose nodes allocate from the
	//     same pool (their tasks are only joined in my IEndFrame).
	// Both edges are declared by ME -- I am the one whose pool is written. Without
	// them the drain recycles a slot the frame still holds, and because the handle
	// carries only a slot id (FRDGBufferRef) a following GetBuffer hands out the
	// wrong buffer: an index buffer bound as a vertex buffer, copy/barrier sizes
	// taken from a descriptor the native no longer has.
	MyStage<IBeginFrame>().IsBlocking<Resource::FResourceSystem>().OnStage<ITick>();
	MyStage<ITick>().IsWaiting<Resource::FResourceSystem>().ForStage<ITick>();

	// The swapchain frame lifecycle is NOT on my two host stages any more: the frame's first stage
	// opens it (FScene::IBeginRender -> BeginSwapchainFrame) and its last one closes it
	// (FScene::IPresent -> EndSwapchainFrame), both inside the collector's graph. THAT is where the
	// frame isolation now lives -- as a declared cross-frame edge in that graph (frame head @N+1
	// waits frame tail @N), because both ends are nodes of that graph. It cannot be declared from
	// here: this is the engine graph, and an edge across the two is a silent no-op.
	//
	// The edge below still pins the HOST chain's frame boundary: without it, this layer's
	// IBeginFrame@N+1 could run while its own ITick@N is still going -- which matters for everything
	// that interleaves with the host stages (the platform's pump, a one-shot install batch).
	MyStage<IBeginFrame>().WaitFor<FRender>().OnLastFrameStage<IEndFrame>();

	// Frame isolation against the PLATFORM, not merely against ourselves. The input snapshot is
	// written by FPlatform::ITick (the pump) and read by the render stages that build a UI frame.
	// Render frames PIPELINE (up to MAHO_FRAMES_IN_FLIGHT), so without this edge a render frame
	// can be reading that snapshot while a LATER platform frame has already overwritten it.
	// Declared here, from the side that knows the platform; the platform never learns about render.
	//
	// NOTE: do NOT also block FPlatform::ITick behind our IEndFrame -- a UI stage waits on
	// FPlatform::ITick (FUIFeature::IInitViews), so a SAME-frame reverse edge closes a cycle
	// (pump@N -> end@N -> read@N -> pump@N) and deadlocks the frame loop outright: a white
	// screen with no diagnostics. And there is no "+1 frame" selector to offset it with either --
	// FEdge::FrameOffset is only ever 0 or -1. Closing the remaining window is therefore NOT an
	// edge problem: the reader has to stop sharing the mutable snapshot with the writer.
	MyStage<IBeginFrame>().WaitFor<Platform::FPlatform>().OnLastFrameStage<IEndFrame>();
}

FRender::~FRender() = default;

void FRender::PreInitialize(FEngineBase&, FEngineContext&)
{
	// Pull up the render features I declare (Render.cplugin Plugins), at the
	// earliest stage of my own lifecycle -- before any of my business init runs.
	// The catalog resolves them by MY name; each is loaded by module base name
	// into MY collector (not the host engine's), so the render graph keeps
	// driving them with FRender& as context. The feature's own declared children
	// (none today) would be installed by that feature into this same collector. The
	// FRAME feature owns the single present point and must load last, so the catalog
	// order is preserved.
	//
	// Loading (DLL + ctor) happens right here; the features' Init stages still
	// run at my own flush safe point (Tick), so their ctors must NOT depend on
	// anything my Initialize sets up -- they only declare stage deps.
	InstallChildrenOf(GetName());
}

void FRender::Initialize(FEngineBase& Engine, FEngineContext& Frame)
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
	ResourcePool = std::make_unique<FRHIResourcePool>(RHI.get());

	// Async shader compiler (dedicated compile thread).
	ShaderCompiler = std::make_unique<FShaderCompilerServer>();
	ShaderCompiler->Initialize();

	// The render graph lives INSIDE the collector (FFrameBuilder::Execute<FRenderStages>()):
	// Flush at frame start, Execute at frame end (see Tick / EndFrame).

	// (The render features are installed in PreInitialize -- they are MY declared
	// sub-plugins; Initialize only sets up the services they will use.)

	// (The UI's CPU-side ImGui context is owned by FUIFeature now; FRender is
	// UI-agnostic and sets nothing up here.)

	// CPU asset -> GPU mirror: listen for imported assets (create the GPU mirror +
	// upload its bulk), for unloaded assets (release the mirror), and provide the
	// GPU fill-back for export. Bound here so the asset system (IInit before mine)
	// is up; unbind in Shutdown.
	if (Resource::FResourceSystem* RS = Resource::GetResourceSystem())
	{
		AssetImportedSub = RS->OnAssetImported.Bind([this](const Name::FName& N, Resource::FOnTransferDone D)
		{
			OnAssetMirrorImported(N, std::move(D));
		});
		AssetUnloadedSub = RS->OnAssetUnloaded.Bind([this](const Name::FName& N, Resource::FOnTransferDone D)
		{
			OnAssetMirrorUnloaded(N, std::move(D));
		});
		AssetCreatedSub = RS->OnAssetCreated.Bind([this](const Name::FName& N, const Resource::FResource& R)
		{
			OnAssetMirrorCreated(N, R);
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

void FRender::PostInitialize(FEngineBase&, FEngineContext&)
{
	// The editor feature (ExampleEditor, Type=Editor) is declared in Render.cplugin's
	// Plugins, so InstallChildrenOf(GetName()) in PreInitialize mounts it into OUR
	// collection -- which is where it belongs: the render graph only sees this collector,
	// and the editor layer mounts render stages (IEditorInput/IEditorCompose), not engine
	// ones. The catalog skips Type=Editor children in Runtime builds, so this replaces the
	// old hand-written #ifdef install; naming a sibling module in C++ here was also what
	// left a SECOND, inert FExampleEditor instance in the host's collector.
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

void FRender::PreShutdown(FEngineBase&, FEngineContext&)
{
}

void FRender::Shutdown(FEngineBase&, FEngineContext&)
{
	// Clean-exit guarantee: drain EVERY async worker BEFORE tearing anything
	// down. The engine's shutdown graph runs the IShutdown stages concurrently,
	// so this layer must be quiescent before we touch shared state -- the render
	// feature graph pool first (a task still in flight holds the graph pointer),
	// then the two threaded servers (shader-compile thread + RHI render-server
	GRender = nullptr;

	// Unbind OUR OWN asset-mirror subscriptions (by id -- RemoveAll() would drop every
	// other subscriber's handlers on the same event) and retract our readback provider.
	// The resource system's Shutdown is ordered AFTER this one (declared in the ctor),
	// so GetResourceSystem() is live here -- every binding is dropped against the live
	// system, never a nulled global accessor. The mirror table entries are Persistent
	// RDG refs owned by the resource pool, released by ResourcePool->Shutdown below --
	// just drop the refs.
	if (Resource::FResourceSystem* RS = Resource::GetResourceSystem())
	{
		RS->OnAssetImported.Unbind(AssetImportedSub);
		RS->OnAssetUnloaded.Unbind(AssetUnloadedSub);
		RS->OnAssetCreated.Unbind(AssetCreatedSub);
		AssetImportedSub = 0;
		AssetUnloadedSub = 0;
		AssetCreatedSub  = 0;
		RS->SetReadback({});
	}
	GpuMirrors.clear();
	// The UI mirror descriptor sets are owned by FUIFeature; it is destroyed by
	// Features.clear() below (its destructor drops the borrowed pool handles).
	// thread). All of them drain their queues and join here.
	//
	// The leftover render tasks drained below (e.g. the last frame's EndFrame)
	// may still present; the swapchain surface is still alive here because
	// FPlatform's Shutdown is ordered AFTER ours (it owns the window/surface the
	// RHI was created from). The RHI stays a stateless task processor -- it
	// never refuses work, it just runs what it is given.
	// Quiescence for the render graph AND its pool: teardown may free feature modules below,
	// and a stage body may have submitted work the graph's own fence does not cover.
	Wait();
	if (ShaderCompiler)
	{
		ShaderCompiler->Shutdown();   // compile thread: drain pending compiles, stop + join
	}
	if (RHI)
	{
		RHI->Shutdown();   // render-server thread: drain + join (device teardown stays in ShutdownRHI)
	}

	// All render work for the last frame was submitted (the graph drives the
	// features synchronously); the GPU may still be executing it. Wait here so
	// every resource destroyed below is released only after the device is idle.
	if (RHI)
	{
		RHI->WaitIdle();

		// The last frame's feature command lists + pooled resources are destroyed
		// below by ResourcePool->Shutdown() while the device is still alive (their
		// VkCommandPools / VkBuffers must not be outstanding when the device dies).

		// Uninstall every render feature through the collector teardown pipeline so
		// each feature's IPreUnInstall stage runs BEFORE its instance is destroyed.
		// FExampleEditor::PreUnInstall uninstalls its editor sub-panels (and the
		// EditorConsole unregisters its live log listener); a bare Features.clear()
		// would destroy the instances without driving those teardown stages and
		// leak subscriptions (e.g. the console's FLog listener) into the Log layer.
		UninstallAll();
		FlushPendingUpdates<TTypeList<IOnInstalled>, TTypeList<IPreUnInstall>>();

		// Destroy whatever the uninstall path could not release (it REFUSES to unload a feature
		// another one still depends on) -- their destructors free Vulkan objects (pipelines /
		// shader modules) that must be released while the device exists. The collector would
		// otherwise destroy them only when this FRender dies, which is after the RHI.
		ReleaseAll();
	}

	// The compile server thread was already joined above; just release the server.
	ShaderCompiler.reset();

	// Release pooled resources before the RHI device goes away. Drain FIRST: the pool teardown is the
	// last lifetime boundary in the layer, so the collector graph has to be quiet -- with frames
	// pipelining, a frame submitted by the last Tick may still have nodes running here, and destroying
	// a command list / buffer it is about to submit is exactly the "destroyed while bound in a
	// command buffer" validation error. (The Wait at the top of this function covers the frames that
	// were already in flight; this one also covers anything the teardown steps above submitted.)
	if (ResourcePool)
	{
		Wait();
		ResourcePool->Shutdown();
		ResourcePool.reset();
	}

	if (RHI)
	{
		RHI->ShutdownRHI();   // re-joining the server thread is a no-op; tears down the device
		RHI.reset();
	}

	// (The UI's CPU-side ImGui context is owned by FUIFeature and is destroyed in
	// its PreUnInstall, ordered here by the collect/shutdown graph.)
}

void FRender::PostShutdown(FEngineBase&, FEngineContext&)
{
}

void FRender::BeginFrame(FEngineBase&, FEngineContext&)
{
	// EMPTY on purpose. The swapchain frame is opened by the frame's FIRST stage
	// (FScene::IBeginRender -> BeginSwapchainFrame), inside the render graph, so its ordering is a
	// declared edge instead of "this host stage happens to run before the batch". The host stages
	// keep their place in the sequence -- other layers interleave with them -- but the render
	// layer's own frame work is graph-scheduled now.
	//
	// (The UI frame feed + NewFrame live in FUIFeature::InitViews; FRender holds no ImGui state.)
}

void FRender::BeginSwapchainFrame()
{
	// Frame head. Everything the frame does is serialized behind this node, and the trace splits the
	// two waits apart, because "the whole frame stalled here" is not actionable on its own.
	if (IRHI* RHIp = RHI.get())
	{
		MAHO_TRACE_SCOPE("FRender::BeginSwapchainFrame.RHI");
		RHIp->BeginFrame();
	}
	MAHO_TRACE_SCOPE("FRender::BeginSwapchainFrame.ResourcePool");
	BeginResourcePool();
}

void FRender::EndSwapchainFrame()
{
	// Frame end: close + submit the frame command list and present. The blit was recorded into that
	// list by the CALLER (the tail stage) just before this, so the close is ordered by statement
	// order -- no graph drain needed to know the recording finished.
	if (IRHI* RHIp = RHI.get())
	{
		MAHO_TRACE_SCOPE("FRender::EndSwapchainFrame.RHI");
		RHIp->EndFrame();
	}
}

void FRender::BeginResourcePool()
{
	if (ResourcePool)
	{
		ResourcePool->BeginFrame();
	}
}

void FRender::Tick(FEngineBase&, FEngineContext&)
{
	// (The whole ImGui frame lifecycle -- feed / NewFrame / build / Render /
	// GetDrawData -- moved into FUIFeature::InitViews inside the render graph below.
	// FRender holds no ImGui state and only schedules.)

	// No frame-start drain any more. The frame boundary is a DECLARED edge inside the collector's
	// own graph (the frame head waits the previous frame's tail), so a full Wait() here would only
	// flatten the ring: frame N+1's batch could never overlap frame N's nodes. Quiescence is the
	// change's business, not every frame's -- FlushPendingUpdates drains (fully: graph + pool) when
	// the frame set actually changes, which is the only event that needed it.
	FlushPendingUpdates<TTypeList<IOnInstalled>, TTypeList<IPreUnInstall>>();

	// The stage SEQUENCE is FRenderStages itself (see Render.h), so the editor-only stages
	// (IEditorInput / IEditorCompose) are part of it in an editor build -- and a stage no
	// installed feature implements is simply not emitted. One call either way. The batch builder
	// re-queries the frame set and reports what it could not bind.
	Execute<FRenderStages>();
	// No trailing wait either: the ring is what keeps the pipeline safe (per-stage self edges +
	// admission), and at shutdown the leftover nodes are drained by FRender::Shutdown's Wait().
}

void FRender::EndFrame(FEngineBase&, FEngineContext&)
{
	// EMPTY on purpose: the frame is closed by its TAIL stage (FScene::IPresent -> EndSwapchainFrame).
	// Closing it here would need "wait for that stage", and the reason that wait existed is gone --
	// the frame command list's only recording (the blit) happens in the same stage that closes it.
	// This is also what un-pins the frame loop: the host may run ahead of the collector by the ring
	// depth, and the RHI's single-depth frame state is protected by the collector's own cross-frame
	// edge (frame head @N+1 waits frame tail @N) instead of by a barrier on this chain.
}

void FRender::RequestExit(FEngineBase&, FEngineContext&)
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

void FRender::AddPass(
	ERHICommandListType PassType,
	FRHIGraphicsPipelineDesc PipelineDesc,
	const FRenderPassDesc& Pass,
	std::function<void(FRHICommandList&)> PassFn,
	std::vector<FRHIDescriptorSet*>* OutDefaultSets)
{
	// Feature-key to ONE pool descriptor-set layout + pipeline layout. The
	// FPassParameter declares the input sets/push-constants; the pool
	// get-or-creates the matching natives. This is the feature value -> pool
	// descriptor layout mapping: the same input binds share one layout.
	std::vector<FRHIDescriptorSetLayout*> SetLayouts;
	std::vector<FRHIDescriptorSetLayoutDesc> SetLayoutDescs;
	SetLayouts.reserve(Pass.Layout.Sets.size());
	SetLayoutDescs.reserve(Pass.Layout.Sets.size());
	for (const FRDGDescriptorSet& SetDesc : Pass.Layout.Sets)
	{
		FRHIDescriptorSetLayoutDesc DSLDesc;
		for (const auto& [Binding, B] : SetDesc.Bindings)
		{
			FRHIDescriptorBinding DB;
			DB.Binding = Binding;
			DB.Type = B.Type;
			DB.Count = 1;
			DB.Stages = B.Stages;
			DSLDesc.Bindings.push_back(DB);
		}
		SetLayoutDescs.push_back(std::move(DSLDesc));
		SetLayouts.push_back(GetOrCreateDescriptorSetLayout(SetLayoutDescs.back()));
	}

	FRHIPipelineLayoutDesc LayoutDesc;
	LayoutDesc.SetLayouts = SetLayouts;
	LayoutDesc.PushConstants = Pass.Layout.PushConstants;
	PipelineDesc.Layout = GetOrCreatePipelineLayout(LayoutDesc);
	FRHIGraphicsPipeline* Pipeline = nullptr;
	if (PipelineDesc.Layout != nullptr)
	{
		Pipeline = GetOrCreateGraphicsPipeline(PipelineDesc);
	}

	// Resolve the declared RDG target into concrete rendering attachments.
	std::vector<FRHIRenderingAttachmentInfo> Colors;
	Colors.reserve(Pass.Target.Color.size());
	for (const auto& A : Pass.Target.Color)
	{
		FRHIRenderingAttachmentInfo Info;
		Info.View = A.View.GetView();
		Info.LoadOp = A.LoadOp;
		Info.StoreOp = A.StoreOp;
		for (std::uint32_t i = 0; i < 4; ++i) { Info.ClearColor[i] = A.ClearColor[i]; }
		Colors.push_back(Info);
	}
	FRHIRenderingAttachmentInfo Depth;
	const FRHIRenderingAttachmentInfo* PDepth = nullptr;
	if (Pass.Target.bHasDepth)
	{
		Depth.View = Pass.Target.Depth.View.GetView();
		Depth.LoadOp = Pass.Target.Depth.LoadOp;
		Depth.StoreOp = Pass.Target.Depth.StoreOp;
		for (std::uint32_t i = 0; i < 4; ++i) { Depth.ClearColor[i] = Pass.Target.Depth.ClearColor[i]; }
		PDepth = &Depth;
	}

	// Snapshot the attachment pointers/extent into locals so the record lambda
	// does not copy the (vector-owning) Colors by value. Width/Height default to
	// the first color attachment's extent when the target did not set them.
	const FRHIRenderingAttachmentInfo* ColorsPtr = Colors.empty() ? nullptr : Colors.data();
	const std::uint32_t ColorCount = static_cast<std::uint32_t>(Colors.size());
	// All attachments share one resolution; infer the render-area extent from the
	// first valid color (falling back to the depth) -- never declared separately.
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
	for (const auto& A : Pass.Target.Color)
	{
		if (A.View.IsValid())
		{
			Width = A.View.GetWidth();
			Height = A.View.GetHeight();
			break;
		}
	}
	if (Width == 0 && Height == 0 && Pass.Target.bHasDepth && Pass.Target.Depth.View.IsValid())
	{
		Width = Pass.Target.Depth.View.GetWidth();
		Height = Pass.Target.Depth.View.GetHeight();
	}

	// Materialise the descriptor sets: resolve each FRDGBinding's ref to a native
	// view/buffer and get-or-create the pool's mutable set. ALL non-per-instance
	// frequencies (Static / PerFrame / PerPass) share ONE implementation path: a
	// persistent mutable set keyed by layout, written at record time via
	// Cmd.UpdateDescriptorSet, before the draws that bind it. The frequency is
	// only a semantic hint about how OFTEN content changes -- every pass parameter
	// is allowed to change, so a Static set is still mutable, just updated far
	// less often than PerFrame / PerPass:
	//   - Static  : per-scene, rarely changes ("变化最小"), but still a mutable set.
	//   - PerFrame: every frame (per-frame GPU scene).
	//   - PerPass : per pass (basepass vs postprocess).
	//   - PerInstance: per mesh batch, and a DIFFERENT path -- a single mutable set
	//     holds one content, so every batch would read the last update. Per-batch
	//     content needs push-descriptor / dynamic-offset UBO, deferred to the
	//     bindless/per-instance change (errors here so it is never mis-baked).
	FRHIGraphicsPipeline* Bound = Pipeline;
	std::uint32_t FirstSet = ~0u;
	for (const FRDGDescriptorSet& SetDesc : Pass.Layout.Sets)
	{
		if (SetDesc.SetIndex < FirstSet) { FirstSet = SetDesc.SetIndex; }
	}
	if (FirstSet == ~0u) { FirstSet = 0; }
	std::uint32_t SetCount = 0;
	for (const FRDGDescriptorSet& SetDesc : Pass.Layout.Sets)
	{
		const std::uint32_t SetSpan = SetDesc.SetIndex - FirstSet + 1;
		if (SetSpan > SetCount) { SetCount = SetSpan; }
	}
	std::vector<FRHIDescriptorSet*> BoundSets(SetCount, nullptr);
	std::vector<FRHIDescriptorSet*> DynamicSets;
	std::vector<std::vector<FRHIDescriptorWrite>> DynamicWrites;
	for (std::size_t I = 0; I < Pass.Layout.Sets.size(); ++I)
	{
		const FRDGDescriptorSet& SetDesc = Pass.Layout.Sets[I];
		std::vector<FRHIDescriptorWrite> Writes;
		for (const auto& [Binding, B] : SetDesc.Bindings)
		{
			FRHIDescriptorWrite W;
			W.Binding = Binding;
			W.Type = B.Type;
				if (const FRDGTextureRef* Tex = std::get_if<FRDGTextureRef>(&B.Resource))
				{
					W.TextureView = Tex->GetView();
					if (B.SamplerIndex >= 0 && static_cast<std::uint32_t>(B.SamplerIndex) < SetDesc.Samplers.size())
					{
						W.Sampler = SetDesc.Samplers[static_cast<std::size_t>(B.SamplerIndex)];
					}
				}
			else if (const FRDGBufferRef* Buf = std::get_if<FRDGBufferRef>(&B.Resource))
			{
				W.Buffer = Buf->GetRHI();
				W.Offset = B.Offset;
				W.Range = B.Range;
			}
			Writes.push_back(W);
		}

		FRHIDescriptorSet* Set = nullptr;
		if (SetDesc.Frequency == EDescriptorSetFrequency::PerInstance)
		{
			MAHO_LOG_CORE_ERROR("FRender::AddPass: PerInstance descriptor set {} -- per-batch content needs push-descriptor / dynamic-offset UBO (deferred to the bindless change)", SetDesc.SetIndex);
		}
		else
		{
			// Static / PerFrame / PerPass share one mechanism: a persistent mutable set keyed by
			// LAYOUT + CONTENT. The pool decides whether WE must write it -- a set that already has
			// the right content must be left alone (that is what makes parallel recording safe), so
			// only freshly created sets enter the write lists.
			bool bNeedsWrite = false;
			Set = GetOrCreateMutableDescriptorSet(SetLayouts[I], SetLayoutDescs[I],
				Writes.data(), static_cast<std::uint32_t>(Writes.size()), bNeedsWrite);
			if (Set != nullptr)
			{
				for (FRHIDescriptorWrite& W : Writes)
				{
					W.Set = Set;
				}
				if (bNeedsWrite)
				{
					DynamicSets.push_back(Set);
					DynamicWrites.push_back(std::move(Writes));
				}
			}
		}
		if (Set != nullptr)
		{
			BoundSets[SetDesc.SetIndex - FirstSet] = Set;
		}
	}

	// Hand the resolved sets to the record lambda BEFORE it runs: the draw-list path needs them to
	// restore the pass's own defaults after a per-batch set displaced them. It used to re-look them
	// up by layout, which only worked while a layout had exactly one set.
	if (OutDefaultSets != nullptr)
	{
		*OutDefaultSets = BoundSets;
	}

	AddPass(PassType, [=](FRHICommandList& List)
	{
		// Write the mutable set contents (host op at record time, before the draws
		// that bind them) so every frequency -- Static / PerFrame / PerPass -- picks
		// up its current content this frame/pass.
		for (std::size_t D = 0; D < DynamicSets.size(); ++D)
		{
			const std::vector<FRHIDescriptorWrite>& DW = DynamicWrites[D];
			List.UpdateDescriptorSet(DynamicSets[D], DW.data(), static_cast<std::uint32_t>(DW.size()));
		}

		// Dynamic rendering: start the feature's render pass, bind the pipeline
		// and its descriptor sets (implicit -- the feature never queries them),
		// then run the draws.
		List.BeginRendering(ColorsPtr, ColorCount, PDepth, Width, Height);
		if (Bound != nullptr)
		{
			List.BindGraphicsPipeline(Bound);
		}
		FRHIDescriptorSet* const* Sets = BoundSets.empty() ? nullptr : BoundSets.data();
		if (Sets != nullptr)
		{
			List.BindDescriptorSets(FirstSet, Sets, static_cast<std::uint32_t>(BoundSets.size()));
		}
		PassFn(List);
		List.EndRendering();
	});
}

void FRender::AddPass(
	ERHICommandListType PassType,
	FRHIGraphicsPipelineDesc PipelineDesc,
	const FRenderPassDesc& Pass,
	const FDrawList& DrawList)
{
	// Declarative draw list -> record lambda, delegated to the typed overload. The
	// typed AddPass does all the resolution once (set layouts/pipeline/sets/target);
	// this wrapper only renders the list's batches. A feature fills the FDrawList
	// (pass-level CPU primitive data + per-batch sets + scissor + push constant) and
	// never touches a vertex/index buffer, a descriptor set or a draw command -- the
	// RHI objects are resolved here, inside AddPass.
	std::uint32_t TargetW = 0;
	std::uint32_t TargetH = 0;
	for (const auto& A : Pass.Target.Color)
	{
		if (A.View.IsValid())
		{
			TargetW = A.View.GetWidth();
			TargetH = A.View.GetHeight();
			break;
		}
	}
	if (TargetW == 0 && TargetH == 0 && Pass.Target.bHasDepth && Pass.Target.Depth.View.IsValid())
	{
		TargetW = Pass.Target.Depth.View.GetWidth();
		TargetH = Pass.Target.Depth.View.GetHeight();
	}

	// The typed overload resolves the pass's descriptor sets; it hands them back here so the record
	// lambda below can restore them after a per-batch set displaces one.
	std::vector<FRHIDescriptorSet*> ResolvedDefaultSets;

	AddPass(PassType, std::move(PipelineDesc), Pass,
		[this, &DrawList, &Pass, TargetW, TargetH, &ResolvedDefaultSets](FRHICommandList& List)
		{
			// The pass-level merged vertex/index buffers were already created + uploaded
			// by the producer (InitViews). AddPass only binds + draws; nothing uploads here.
			if (!DrawList.HasPrimitiveData())
			{
				return;
			}

			if (DrawList.HasPushConstants())
			{
				List.PushConstants(DrawList.GetPushConstantStages(), 0, DrawList.GetPushConstantSize(), DrawList.GetPushConstantData());
			}
			List.SetViewport(0.0f, 0.0f, static_cast<float>(TargetW), static_cast<float>(TargetH));

			// Pass-level DEFAULT descriptor sets, resolved by the TYPED AddPass and handed to us
			// through its optional out-param. They matter because a per-batch set below REPLACES
			// set N for ONE draw and the binding stays until re-bound -- so a text/icon batch after
			// an Image would sample the Image's texture. A batch with no per-batch set restores
			// these, keeping every draw on the correct set.
			//
			// This used to re-look the sets up by LAYOUT, which only worked while a layout had
			// exactly one set -- the very sharing this change removes.
			std::uint32_t DefaultFirstSet = 0;
			for (const FRDGDescriptorSet& SetDesc : Pass.Layout.Sets)
			{
				if (SetDesc.SetIndex < DefaultFirstSet) { DefaultFirstSet = SetDesc.SetIndex; }
			}
			const std::vector<FRHIDescriptorSet*>& DefaultSets = ResolvedDefaultSets;
			// A per-batch set is displaced by a following text/icon batch; re-bind the
			// defaults for every set index when a batch carries no per-batch set.
			const auto BindDefaultSets = [&](FRHICommandList& L)
			{
				for (std::uint32_t I = 0; I < DefaultSets.size(); ++I)
				{
					FRHIDescriptorSet* S = DefaultSets[I];
					if (S != nullptr)
					{
						L.BindDescriptorSets(DefaultFirstSet + I, &S, 1);
					}
				}
			};

			for (const FDrawBatch& B : DrawList.GetBatches())
			{
				// Per-batch descriptor sets: resolve each by CONTENT (content-addressable
				// get-or-create) and bind it in place of the pass-level default for this
				// draw only. An ImDrawCmd that switches texture => a distinct per-batch set.
				bool bBoundPerBatch = false;
				for (const FRDGDescriptorSet& BS : B.Sets)
				{
					FRHIDescriptorSetLayoutDesc DSLDesc;
					for (const auto& [Binding, Bnd] : BS.Bindings)
					{
						FRHIDescriptorBinding DB;
						DB.Binding = Binding;
						DB.Type = Bnd.Type;
						DB.Count = 1;
						DB.Stages = Bnd.Stages;
						DSLDesc.Bindings.push_back(DB);
					}
					FRHIDescriptorSetLayout* PerLayout = GetOrCreateDescriptorSetLayout(DSLDesc);
					if (PerLayout == nullptr)
					{
						continue;
					}
					std::vector<FRHIDescriptorWrite> BWrites;
					for (const auto& [Binding, Bnd] : BS.Bindings)
					{
						FRHIDescriptorWrite W;
						W.Binding = Binding;
						W.Type = Bnd.Type;
						if (const FRDGTextureRef* Tex = std::get_if<FRDGTextureRef>(&Bnd.Resource))
						{
							W.TextureView = Tex->GetView();
							if (Bnd.SamplerIndex >= 0 && static_cast<std::uint32_t>(Bnd.SamplerIndex) < BS.Samplers.size())
							{
								W.Sampler = BS.Samplers[static_cast<std::size_t>(Bnd.SamplerIndex)];
							}
						}
						else if (const FRDGBufferRef* Buf = std::get_if<FRDGBufferRef>(&Bnd.Resource))
						{
							W.Buffer = Buf->GetRHI();
							W.Offset = Bnd.Offset;
							W.Range = Bnd.Range;
						}
						BWrites.push_back(W);
					}
					FRHIDescriptorSet* PerSet = GetOrCreateDescriptorSet(PerLayout, DSLDesc, BWrites.data(), static_cast<std::uint32_t>(BWrites.size()));
					if (PerSet != nullptr)
					{
						List.BindDescriptorSets(BS.SetIndex, &PerSet, 1);
						bBoundPerBatch = true;
					}
				}
				if (!bBoundPerBatch)
				{
					// No per-batch set (text/icon/button): restore the pass-level default so
					// the previous Image's texture does not leak into this draw.
					BindDefaultSets(List);
				}

				// Geometry source: pass-level GPU buffer (slice) > batch-owned buffer.
				FRHIBuffer* VxBuf = nullptr;
				FRHIBuffer* IxBuf = nullptr;
				if (DrawList.GetVertexBuffer().IsValid())
				{
					VxBuf = DrawList.GetVertexBuffer().GetRHI();
					if (DrawList.GetIndexBuffer().IsValid())
					{
						IxBuf = DrawList.GetIndexBuffer().GetRHI();
					}
				}
				else if (B.VertexBuffer.IsValid())
				{
					VxBuf = B.VertexBuffer.GetRHI();
					if (B.IndexBuffer.IsValid())
					{
						IxBuf = B.IndexBuffer.GetRHI();
					}
				}

				if (B.bHasScissor)
				{
					List.SetScissor(B.ScissorX, B.ScissorY, B.ScissorW, B.ScissorH);
				}
				else
				{
					List.SetScissor(0, 0, TargetW, TargetH);
				}

				if (IxBuf != nullptr && B.IndexCount > 0)
				{
					if (VxBuf != nullptr)
					{
						List.BindVertexBuffer(0, VxBuf, B.VertexOffset);
					}
					List.BindIndexBuffer(IxBuf, B.IndexOffset, B.bIndex32);
					List.DrawIndexed(B.IndexCount, B.InstanceCount, 0, 0, 0);
				}
				else if (VxBuf != nullptr)
				{
					List.BindVertexBuffer(0, VxBuf, B.VertexOffset);
					List.Draw(B.VertexCount, B.InstanceCount, 0, 0);
				}
				else if (B.VertexCount > 0)
				{
					// No vertex buffer: primitive generated in-shader (gl_VertexIndex).
					List.Draw(B.VertexCount, B.InstanceCount, B.VertexOffset, 0);
				}
			}
		},
		&ResolvedDefaultSets);
}

void FRender::AddPass(ERHICommandListType PassType, std::function<void(FRHICommandList&)> PassFn)
{
	// Record only -- no wait, no fence, no submit, and no critical section: each pass owns its own
	// command list, so passes record in parallel on their own node threads. The frame's IPresent
	// submits the tables in registration order, which is the order the stage nodes ran (the declared
	// edge order). A later pass therefore never waits for an earlier one, and nothing here can race an
	// in-flight submit: this frame's lists are not on the queue yet.
	FRHICommandList* List = RecordPass(PassFn);
	if (List == nullptr)
	{
		return;
	}

	// Which frame does this pass belong to? The one whose stage call is running on THIS thread --
	// the dispatcher publishes it for the duration of the call. Reading it per CALL (rather than
	// from a collector member) is what keeps the registration in the right frame when frames
	// overlap: an in-flight node of frame N would otherwise be attributed to whichever frame the
	// dispatcher had prepared last.
	// Which frame does this pass belong to? The one whose stage call is running on THIS thread --
	// the dispatcher publishes it for the duration of the call. Reading it per CALL (rather than
	// from a collector member) is what keeps the registration in the right frame when frames
	// overlap: an in-flight node of frame N would otherwise be attributed to whichever frame the
	// dispatcher had prepared last.
	FRenderContext* Frame = static_cast<FRenderContext*>(GetCurrentFrameContext());
	if (Frame != nullptr)
	{
		std::lock_guard<std::mutex> Lock(Frame->Mutex);
		Frame->Passes.push_back({List, PassType});
		return;
	}

	// No ambient context: recorded OUTSIDE any stage. Nothing about this pass belongs to a frame, so
	// it cannot go into a frame's table (there is none to pick) -- it goes to the off-frame one,
	// drained first by the next IPresent.
	std::lock_guard<std::mutex> Lock(OffFramePassMutex);
	OffFramePasses.push_back({List, PassType});
}

FRHICommandList* FRender::RecordPass(const std::function<void(FRHICommandList&)>& PassFn)
{
	FRHICommandList* List = ResourcePool ? ResourcePool->AcquireRenderList() : nullptr;
	if (List == nullptr)
	{
		return nullptr;
	}
	List->Begin();
	PassFn(*List);
	List->End();
	return List;
}

void FRender::SubmitRecordedPasses(FRenderContext& Frame)
{
	IRHI* P = RHI.get();
	if (P == nullptr)
	{
		return;
	}

	// Take both tables out under their locks, then submit without holding either: what is being
	// swapped was recorded before this stage ran (the frame's nodes are done -- this stage is last
	// in the sequence).
	std::vector<FRenderContext::FPendingPass> OffFramePasses;
	{
		std::lock_guard<std::mutex> Lock(OffFramePassMutex);
		OffFramePasses.swap(this->OffFramePasses);
	}
	std::vector<FRenderContext::FPendingPass> Passes;
	{
		std::lock_guard<std::mutex> Lock(Frame.Mutex);
		Passes.swap(Frame.Passes);
	}

	// Submission is the handover point for the lists' lifetime: from here on they belong to the pool,
	// which destroys them at the next frame boundary (after it waited the frame fence). Until now
	// they were owned by the table they sat in -- and a table may hold a list recorded BEFORE the
	// frame loop started (the asset-mirror upload at init), which must survive until this point.
	std::vector<FRHICommandList*> Submitted;
	Submitted.reserve(OffFramePasses.size() + Passes.size());
	for (const FRenderContext::FPendingPass& Pass : OffFramePasses)
	{
		Submitted.push_back(Pass.List);
	}
	for (const FRenderContext::FPendingPass& Pass : Passes)
	{
		Submitted.push_back(Pass.List);
	}

	// ONE hop for the whole batch: the server thread runs the loop below inline (Submit's marshal
	// is re-entrant), so the frame costs a single task and the queue still sees exactly one emitter.
	// The batch is what `RHI::EndFrame` then queues the frame command list behind -- same queue,
	// FIFO, so the passes precede the present blit.
	P->RunOnServer([P, OffFramePasses = std::move(OffFramePasses), Passes = std::move(Passes)]
	{
		for (const FRenderContext::FPendingPass& Pass : OffFramePasses)
		{
			P->Submit(Pass.List, Pass.Type);
		}
		for (const FRenderContext::FPendingPass& Pass : Passes)
		{
			P->Submit(Pass.List, Pass.Type);
		}
	});

	if (ResourcePool)
	{
		ResourcePool->RetireRenderLists(Submitted);
	}
}

void* FRender::AllocParameterBytes(std::size_t Size, std::size_t Align)
{
	return ResourcePool ? ResourcePool->AllocateFrameTransient(Size, Align) : nullptr;
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

FRHIDescriptorSet* FRender::GetOrCreateDescriptorSet(
	FRHIDescriptorSetLayout* Layout,
	const FRHIDescriptorSetLayoutDesc& LayoutDesc,
	const FRHIDescriptorWrite* Writes,
	std::uint32_t WriteCount)
{
	return ResourcePool ? ResourcePool->GetOrCreateDescriptorSet(Layout, LayoutDesc, Writes, WriteCount) : nullptr;
}

FRHIDescriptorSet* FRender::GetOrCreateMutableDescriptorSet(
	FRHIDescriptorSetLayout* Layout,
	const FRHIDescriptorSetLayoutDesc& LayoutDesc,
	const FRHIDescriptorWrite* Writes,
	std::uint32_t WriteCount,
	bool& bOutNeedsWrite)
{
	if (!ResourcePool)
	{
		bOutNeedsWrite = false;
		return nullptr;
	}
	return ResourcePool->GetOrCreateMutableDescriptorSet(Layout, LayoutDesc, Writes, WriteCount, bOutNeedsWrite);
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

void FRender::SetPresentTarget(const FRDGTextureRef& Texture)
{
	PresentTarget = Texture;
}

FRDGTextureRef FRender::GetPresentTarget() const
{
	return PresentTarget;
}

// -- CPU asset -> GPU mirror --

namespace
{
ERHIFilter SamplerFilterMirror(Resource::ETextureSamplerMode Mode)
{
	return Mode == Resource::ETextureSamplerMode::Nearest ? ERHIFilter::Nearest : ERHIFilter::Linear;
}

ERHIAddressMode SamplerAddressMirror(Resource::ETextureAddressMode Mode)
{
	switch (Mode)
	{
		case Resource::ETextureAddressMode::MirroredRepeat: return ERHIAddressMode::MirroredRepeat;
		case Resource::ETextureAddressMode::ClampToEdge:    return ERHIAddressMode::ClampToEdge;
		case Resource::ETextureAddressMode::ClampToBorder: return ERHIAddressMode::ClampToBorder;
		default:                                            return ERHIAddressMode::Repeat;
	}
}
} // namespace

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
		case Resource::ETexturePixelFormat::D32Sfloat:
			return ERHIFormat::D32_SFLOAT;
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
	// PERSISTENT, not Transient: the pass below is recorded OFF the frame graph, so it is submitted
	// by the NEXT frame's IPresent -- while the frame's head has already recycled every transient.
	// A frame-scoped slot would therefore be handed to another allocation (and its native destroyed
	// when the descriptor differs) before this list ever reached the queue: Vulkan reports it as a
	// buffer "destroyed" while bound in a command buffer being submitted. A frame-external pass needs
	// a frame-external buffer; the pool keeps one per size and reuses it for the next same-size import.
	FRDGBufferRef StagingRef = CreateBuffer(Staging, ERDGResourceLifetime::Persistent);
	if (!StagingRef.IsValid() || StagingRef.GetRHI() == nullptr)
	{
		ReleaseTexture(TexRef);
		MAHO_LOG_CORE_ERROR("FRender: mirror staging buffer allocation failed");
		return false;
	}

	// Commit the mirror (Persistent texture) before the transfer submit so a later
	// unload / export sees it.
	GpuMirrors[AssetName] = TexRef;

	// The UI image handle is built ON DEMAND by FUIFeature (which owns the mirror
	// set map): it get-or-creates a set-0 CombinedImageSampler descriptor set and
	// uses the handle as ImTextureID for ImGui::Image. FRender only commits the GPU
	// mirror; the descriptor-set half lives in the UI feature.

	// One transfer pass outside a render pass: copy the CPU pixels into a staging
	// buffer then CopyBufferToTexture. UIFeature::UploadFont follows the same
	// pattern. Pixels are copied synchronously during record (still valid here --
	// Done() below is what drops the CPU bulk).
	//
	// This runs on the resource system's IO thread, outside any stage call, so AddPass sees no
	// ambient frame context and files the pass in the OFF-FRAME table -- submitted (first) by the
	// next frame's IPresent, which keeps the copy ahead of the draws that sample the mirror.
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
	});
	return true;
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
	GpuSamplers.erase(AssetName);
	if (Done)
	{
		Done(true, std::string_view());
	}
}

void FRender::OnAssetMirrorCreated(const Name::FName& AssetName, const Resource::FResource& Resource)
{
	// A same-named mirror already exists (re-create). Keep the existing one - the
	// caller must DestroyResource first to rebuild a mirror with new fields.
	if (GpuMirrors.find(AssetName) != GpuMirrors.end())
	{
		return;
	}

	// Only textures are creatable resources today (FTexture2D desc). Build a
	// Persistent GPU mirror from the descriptor fields; empty Pixels = no staged
	// upload, the mirror is a runtime placeholder render features sample.
	const Resource::FTexture* Tex = dynamic_cast<const Resource::FTexture*>(&Resource);
	if (Tex == nullptr)
	{
		MAHO_LOG_CORE_WARN("FRender: mirror create asset={} (non-texture, skipped)", AssetName.ToString());
		return;
	}
	if (Tex->GetWidth() == 0 || Tex->GetHeight() == 0)
	{
		MAHO_LOG_CORE_WARN("FRender: mirror create asset={} (zero extent, skipped)", AssetName.ToString());
		return;
	}

	const ERHIFormat Fmt = FormatMirror(Tex->GetPixelFormat(), Tex->IsSRGB());
	if (Fmt == ERHIFormat::Unknown)
	{
		MAHO_LOG_CORE_WARN("FRender: mirror create asset={} (unsupported format, skipped)", AssetName.ToString());
		return;
	}

	FRHITextureDesc Desc;
	Desc.Format = Fmt;
	Desc.Dimension = DimensionMirror(Tex->GetDimension());
	Desc.Extent.Width = Tex->GetWidth();
	Desc.Extent.Height = Tex->GetHeight();
	Desc.Extent.Depth = Tex->GetDepth();
	Desc.MipLevels = Tex->GetMipCount();
	Desc.ArrayLayers = Tex->GetArrayLayers();
	switch (Tex->GetMirrorUsage())
	{
		case Resource::ETextureMirrorUsage::ColorTarget:
			Desc.Usage = ERHITextureUsage::ColorAttachment | ERHITextureUsage::Sampled | ERHITextureUsage::TransferSrc;
			break;
		case Resource::ETextureMirrorUsage::DepthTarget:
			Desc.Usage = ERHITextureUsage::DepthStencil | ERHITextureUsage::Sampled;
			break;
		default:
			Desc.Usage = ERHITextureUsage::Sampled | ERHITextureUsage::TransferDst;
			break;
	}
	Desc.MemoryUsage = ERHIMemoryUsage::GPUOnly;

	FRDGTextureRef TexRef = CreateTexture(Desc, ERDGResourceLifetime::Persistent);
	if (!TexRef.IsValid() || TexRef.GetRHI() == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FRender: mirror create texture allocation failed asset={}", AssetName.ToString());
		return;
	}
	GpuMirrors[AssetName] = TexRef;

	// Mirror sampler from the asset's GPU sampling config (filter + wrap + lod bias),
	// created through the pool (get-or-create, pool-shared). Resolved by features via
	// GetMirrorSampler(FName) alongside the texture mirror.
	{
		FRHISamplerDesc SDesc;
		SDesc.MinFilter = SamplerFilterMirror(Tex->GetFilterMode());
		SDesc.MagFilter = SamplerFilterMirror(Tex->GetFilterMode());
		SDesc.AddressU = SamplerAddressMirror(Tex->GetAddressU());
		SDesc.AddressV = SamplerAddressMirror(Tex->GetAddressV());
		SDesc.AddressW = SamplerAddressMirror(Tex->GetAddressW());
		SDesc.LodBias = Tex->GetLodBias();
		if (FRHISampler* Sampler = CreateSampler(SDesc))
		{
			GpuSamplers[AssetName] = Sampler;
		}
	}

	MAHO_LOG_CORE_INFO("FRender: mirror created asset={} ({}x{}, {})", AssetName.ToString(),
		Tex->GetWidth(), Tex->GetHeight(), Tex->IsSRGB() ? "sRGB" : "linear");
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
extern "C" MAHO_RENDER_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FRender::CreateFrame();
}
