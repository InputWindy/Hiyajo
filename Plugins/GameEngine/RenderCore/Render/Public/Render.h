#pragma once

#include "RenderApi.h"
#include <Maho.h>
#include <Engine/Frame.h>
#include <Engine/FrameBuilder.h>
#include <Engine/Engine.h>
#include <RHI/RHIServer.h>
#include "RDG.h"
#include "RenderDrawList.h"
#include "ShaderCompiler.h"
#include "ShaderParameterStruct.h"
#include <Resource.h>
#include <AssetTypes.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Maho
{

class FRender;
class FRHIResourcePool;

template <typename T> class TShaderHandle;

/** Per-frame state for the RENDER stages (one instance per ring slot; reach it as
 *  `FRender::FContext`).
 *
 *  DEFINED HERE, at namespace scope, and not inside the class: the stage interfaces below must
 *  name it in their signatures, and they are declared before `FRender` exists. `FRender` carries
 *  a nested alias so stages and the dispatch macro can still write the nested name.
 *
 *  The members are the frame's RECORDED PASSES, in submission order -- the state the whole
 *  "record now, submit once at the end" model hangs off. It is per-slot on purpose: it is the
 *  COLLECTOR graph's frame state, so only stages driven by that graph (whose slot comes from its
 *  own counter) may touch it -- the engine-side host chain must never index it (see
 *  `refactor-per-frame-context` D8). */
struct FRenderContext
{
	/** One pass recorded this frame, waiting for the frame's IPresent to submit it. */
	struct FPendingPass
	{
		FRHICommandList* List = nullptr;
		ERHICommandListType Type = ERHICommandListType::Graphics;
	};

	/** A frame's stage nodes record concurrently (they are separate graph nodes on pool threads),
	 *  so the table they all append to needs a lock. */
	std::mutex Mutex;
	std::vector<FPendingPass> Passes;
};

namespace Detail
{
	/** Resolve a shader type's optional static entry point (default "main"). */
	template <typename T>
	inline const char* GetVertexEntryPoint()
	{
		if constexpr (requires { T::GetVertexEntryPoint(); })
		{
			return T::GetVertexEntryPoint();
		}
		else
		{
			return "main";
		}
	}

	template <typename T>
	inline const char* GetFragmentEntryPoint()
	{
		if constexpr (requires { T::GetFragmentEntryPoint(); })
		{
			return T::GetFragmentEntryPoint();
		}
		else
		{
			return "main";
		}
	}
}

/** Global render instance accessor (cross-DLL via function, no bare variable
 *  export) -- set when the Render layer initializes. Other layers (e.g. the
 *  ImGui layer) use it to reach the RHI. */
MAHO_RENDER_API FRender* GetRender();

class MAHO_RENDER_API IOnInstalled
{
public:
	virtual ~IOnInstalled() = default;
	virtual void OnInstalled(FRender&, FRenderContext&) = 0;
};

class MAHO_RENDER_API IInitViews
{
public:
	virtual ~IInitViews() = default;
	virtual void InitViews(FRender&, FRenderContext&) = 0;
};

class MAHO_RENDER_API IBeginRender
{
public:
	virtual ~IBeginRender() = default;
	virtual void BeginRender(FRender&, FRenderContext&) = 0;
};

class MAHO_RENDER_API IRender
{
public:
	virtual ~IRender() = default;
	virtual void Render(FRender&, FRenderContext&) = 0;
};

class MAHO_RENDER_API IEndRender
{
public:
	virtual ~IEndRender() = default;
	virtual void EndRender(FRender&, FRenderContext&) = 0;
};

class MAHO_RENDER_API IPostProcess
{
public:
	virtual ~IPostProcess() = default;
	virtual void PostProcess(FRender&, FRenderContext&) = 0;
};

class MAHO_RENDER_API IRenderUI
{
public:
	virtual ~IRenderUI() = default;
	virtual void RenderUI(FRender&, FRenderContext&) = 0;
};

#ifdef MAHO_EDITOR_BUILD
/**
 * Pass0 (input takeover): runs FIRST in the editor frame, BEFORE the game-UI
 * feature's IInitViews feeds + NewFrame's its IO. The editor feature implements
 * this stage to taste the Win32 input, re-base it to the viewport panel rect and
 * feed the GAME-UI context's IO (FUIFeature::SetEditorInput), so the game UI only
 * responds inside the viewport panel and its layout matches the displayed
 * (panel-scaled) on-screen surface instead of the whole window. Declared under
 * MAHO_EDITOR_BUILD: with no editor feature installed the stage has no
 * implementer and is silently skipped (no regression).
 */
class MAHO_RENDER_API IEditorInput
{
public:
	virtual ~IEditorInput() = default;
	virtual void EditorInput(FRender&, FRenderContext&) = 0;
};

/**
 * Pass3: editor final-compose surface. Runs AFTER the game UI (IRenderUI)
 * composites onto its off-screen target, and BEFORE IPresent blits to the
 * swapchain. When an editor build is present, an editor feature implements this
 * stage to sample the game-UI composite, draw the editor overlay on top, and set
 * THAT target as the frame's present target. Originally an editor-only stage, so
 * it is only selected into the render graph under MAHO_EDITOR_BUILD; with no
 * editor feature installed the stage has no implementer and is silently skipped,
 * so the game-UI target stays the present target (no regression).
 */
class MAHO_RENDER_API IEditorCompose
{
public:
	virtual ~IEditorCompose() = default;
	virtual void EditorCompose(FRender&, FRenderContext&) = 0;
};
#endif // MAHO_EDITOR_BUILD

/**
 * The frame's LAST stage in the sequence, and the frame's single submission point. Implemented by
 * the frame feature (FScene) -- it used to have no implementer at all, because the blit was issued
 * from the host chain (`FRender::EndFrame`) instead.
 *
 * Two things happen here, in order:
 *   1. `FRender::SubmitRecordedPasses` submits every pass recorded this frame, in registration
 *      order (= the order the stage nodes ran), plus any pass recorded off-frame (the asset-mirror
 *      upload);
 *   2. `FRender::PresentTexture(R.GetPresentTarget())` records the blit that puts the frame's final
 *      target on the swapchain backbuffer.
 *
 * RECORDING IT HERE is what makes "the present target's last writer runs before the blit" a
 * declared edge instead of a consequence of draining the whole graph (the host `IEndFrame` used to
 * wait the graph, and that wait WAS the ordering).
 *
 * Being last in the SEQUENCE is not an ordering by itself: the scheduler emits a stage chain only
 * between a frame's OWN stages and has no stage barrier. So every stage that records a pass must
 * declare itself against this one (`MyStage<X>().IsBlocking<Scene::FScene>().OnStage<IPresent>()`),
 * and so must every writer of the present target. A recorder that misses its edge does not lose
 * much on paper and everything in practice: its pass is registered after this stage has already
 * taken the table, so it is submitted a whole ring later against resources its frame has released.
 */
class MAHO_RENDER_API IPresent
{
public:
	virtual ~IPresent() = default;
	virtual void Present(FRender&, FRenderContext&) = 0;
};

class MAHO_RENDER_API IPreUnInstall
{
public:
	virtual ~IPreUnInstall() = default;
	virtual void PreUnInstall(FRender&, FRenderContext&) = 0;
};

// The per-stage dispatch specializations live AT THE BOTTOM of this header, after `class FRender`:
// their body names `FRender::FContext`, which is only declared inside the class.

/**
 * Render subsystem - a layer in the host engine (mounted as IInit/ITick/...),
 * plus its own layer collector for render features (implementing IBeginRender/
 * IRender/IEndRender/IPresent). The dedicated threads live INSIDE the pieces it
 * owns: the RHI (FRHI) is a render server (FThreadedServer) and the shader
 * compiler (FShaderCompilerServer) is its own FThreadedServer - FRender itself
 * is NOT a server. The host engine only sees FRender as one layer; render
 * features are installed and scheduled entirely inside FRender.
 *
 * FRender is also the RDG resource pool: features create off-screen resources
 * through CreateTexture/CreateBuffer and hand back FRDG*Ref handles; the native
 * FRHITexture/FRHIBuffer live in the pool (cross-frame reuse). The swapchain
 * backbuffer is only ever touched by the frame feature (via RHI->PresentTexture
 * on GetRHI()).
 */
class MAHO_RENDER_API FRender
	: public FFrameExtension, public IPipeline<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>
	, public FFrameBuilder<FRender>
{
public:
	/** Per-frame state for the render stages -- one instance per ring slot, so a stage of frame N
	 *  and the same stage of frame N+1 never touch the same state. (Same shape as
	 *  FEngineBase::FContext; the reasoning lives there.) EMPTY FOR NOW: this step only establishes
	 *  the plumbing.
	 *
	 *  An ALIAS, not the definition: the stage interfaces must name this type before FRender
	 *  exists, so the definition lives at namespace scope (see FRenderContext). Stages and the
	 *  dispatch macro write the nested name; it is the same type either way. */
	using FContext = FRenderContext;

protected:
	/** One context per ring slot. The derived class owns the storage -- the base cannot name the
	 *  nested type while it is being instantiated. */
	std::array<FContext, MAHO_FRAMES_IN_FLIGHT> Slots;

	/** Type-erased access to a slot's context (see FFrameBuilder). Must never return nullptr. */
	void* GetContext(int Slot) override
	{
		// The dispatcher publishes this pointer as the AMBIENT frame context for the duration of each
		// stage call (GetCurrentFrameContext), which is how a helper reached from a stage body -- the
		// frame's own AddPass -- knows which frame it belongs to. It is deliberately NOT remembered
		// here: frames overlap now, so a member would attribute a late-running node's work to whichever
		// frame the dispatcher last prepared.
		return &Slots[Slot];
	}

private:
	MAHO_DECLARE_FRAME(FRender);

	FRender();
	~FRender() override;
public:
	/** CPU-asset->GPU mirror map access for UI features: the UIFeature draws each
	 *  texture mirror as an ImGui::Image, re-resolving its set-0 CombinedImageSampler
	 *  descriptor set from the resource pool on demand (content-addressable get-or-create)
	 *  and sizing the image from this mirror's RDG texture. */
	[[nodiscard]] const std::unordered_map<Name::FName, FRDGResourceRef>& GetMirrors() const { return GpuMirrors; }
	[[nodiscard]] const FRDGResourceRef* GetMirror(const Name::FName& AssetName) const
	{
		const auto It = GpuMirrors.find(AssetName);
		return It != GpuMirrors.end() ? &It->second : nullptr;
	}

	/** Sampler mirror for an asset texture (built from its GPU sampling config in
	 *  OnAssetMirrorCreated). Feature draws resolve this to bind the sampled texture.
	 *  Returns nullptr when the asset has no texture mirror or no sampler was created. */
	[[nodiscard]] FRHISampler* GetMirrorSampler(const Name::FName& AssetName) const
	{
		const auto It = GpuSamplers.find(AssetName);
		return It != GpuSamplers.end() ? It->second : nullptr;
	}
public:
	// -- render surface / canvas info + present. FRender owns the RHI; features
	// never reach the raw IRHI* (no GetRHI()). The canvas is the swapchain geometry
	// the scene color target is sized to, and the swapchain format it is created in.
	[[nodiscard]] std::uint32_t GetCanvasWidth() const;
	[[nodiscard]] std::uint32_t GetCanvasHeight() const;
	[[nodiscard]] ERHIFormat GetSwapchainFormat() const;
	/** Blit a scene-color RDG texture to the swapchain backbuffer (the frame's IPresent point). */
	void PresentTexture(const FRDGTextureRef& Texture);

	/**
	 * Frame HEAD, called by the frame feature's IBeginRender stage: open the swapchain frame (the RHI
	 * waits the previous frame's fence, resets it, acquires an image, begins the frame command list)
	 * and advance the resource pool (destroy the finished frame's submitted lists, recycle transients,
	 * sweep descriptor sets nothing asked for).
	 *
	 * Both are FRAME primitives, so both belong to the frame's first stage: that makes their ordering
	 * a declared edge instead of "a host stage happened to run before the graph". Everything that
	 * acquires a list or allocates from the pool must be ordered after this -- the same discipline the
	 * tail stage (IPresent) needs, from the other end of the frame.
	 */
	void BeginSwapchainFrame();

	/**
	 * Frame END, called by the frame feature's IPresent stage AFTER it submitted the frame's passes
	 * and recorded the blit: close the frame command list, submit it and present. Because the blit is
	 * recorded in that same stage just before this call, "close after the recording finished" is a
	 * statement order -- which is exactly why the host's `Wait()` is gone.
	 */
	void EndSwapchainFrame();

	/**
	 * Submit everything this frame recorded, in registration order: the passes recorded OFF the
	 * frame graph first (an asset-mirror upload records on the resource IO thread, between this
	 * frame's IBeginFrame and ITick, where "this frame" is not yet known -- see PendingOffFramePasses),
	 * then this frame's table. The submit runs on the RHI server thread (one hop for the whole
	 * batch), so the frame's lists reach the queue before the frame command list that
	 * `RHI::EndFrame` submits afterwards -- which is what puts them before the present blit, on the
	 * same queue's FIFO. Called by the frame's IPresent stage; nothing else submits a pass list.
	 */
	void SubmitRecordedPasses(FRenderContext& Frame);

	/**
	 * Set the FINAL on-screen present target for this frame. Any UI feature that
	 * composes the last on-screen surface calls this after drawing (RenderUI), and the
	 * frame feature's IPresent blits it to the swapchain. Last writer wins: in a
	 * runtime build the game UI sets GameRT; in an editor build the editor UI sets
	 * EditorRT after compositing, so the editor's surface is what gets presented.
	 */
	void SetPresentTarget(const FRDGTextureRef& Texture);
	/** The current present target (set by a UI feature; default empty => no present). */
	[[nodiscard]] FRDGTextureRef GetPresentTarget() const;

	// -- RDG resource pool (off-screen resources) --
	[[nodiscard]] FRDGTextureRef CreateTexture(const FRHITextureDesc& Desc, ERDGResourceLifetime Lifetime = ERDGResourceLifetime::Persistent);
	[[nodiscard]] FRDGBufferRef CreateBuffer(const FRHIBufferDesc& Desc, ERDGResourceLifetime Lifetime = ERDGResourceLifetime::Persistent);
	void ReleaseTexture(FRDGTextureRef& Ref);
	void ReleaseBuffer(FRDGBufferRef& Ref);

	/** Record a render-feature pass: acquire a command list, record on the CALLING node thread, and
	 *  register the list in this frame's ordered table. It does NOT submit -- the frame's IPresent
	 *  submits the table once, in registration order (which is the order the stage nodes ran, i.e.
	 *  the declared edge order). Order between passes is therefore a graph property, not a call-site
	 *  one: passes record in PARALLEL, so a feature must not rely on "my AddPass ran before that
	 *  one's" unless it declared an edge against it. */
	void AddPass(ERHICommandListType PassType, std::function<void(FRHICommandList&)> PassFn);

	/**
	 * Declarative draw-list pass: consumes a FDrawList INSTEAD of a record lambda.
	 * The pass-level GPU vertex/index buffers are already created + uploaded by the
	 * producer (InitViews, stored as FRDGBufferRefs); AddPass binds the pass-level
	 * sets, then for each batch records: bind its
	 * per-batch descriptor sets (content-addressable -- a repeated same-resource batch
	 * reuses one pooled set), bind geometry, set its scissor, PushConstants and draw.
	 *
	 * This hides every RHI object from the feature: it never holds a vertex/index
	 * buffer, a descriptor set, a scissor or a draw command -- it only fills the
	 * FDrawList (geometry slices + per-batch sets + scissor + push constant). ImGui is
	 * the canonical consumer (each ImDrawCmd's texture switch => a per-batch set).
	 */
	void AddPass(
		ERHICommandListType PassType,
		FRHIGraphicsPipelineDesc PipelineDesc,
		const FRenderPassDesc& Pass,
		const FDrawList& DrawList);

	// -- compile-time FParameters (macro-declared) passes ------------------------
	// The feature declares a TParameters struct via BEGIN/SHADER_PARAMETER/END,
	// allocates it with FRender::AllocParameters<T>() (pool frame-transient) and
	// passes a POINTER to AddPass. AddPass translates the compile-time metadata
	// (ShaderParameterBuild) into a runtime FPassParameter (descriptor sets +
	// push-constant range), builds the pipeline, and binds the push-constant block
	// (built from the struct members' values) right before the draw lambda runs.
	// For the draw-list path the layout is built the same way, but push-constant
	// data comes from FDrawList::SetPushConstants.
	template <typename TParameters>
	void AddPass(
		ERHICommandListType PassType,
		FRHIGraphicsPipelineDesc PipelineDesc,
		const FRenderTarget& Target,
		const TParameters* Parameters,
		std::function<void(FRHICommandList&)> PassFn)
	{
		FShaderParameterBuildResult Built = ShaderParameterBuild(*Parameters);
		FRenderPassDesc Pass;
		Pass.Layout = std::move(Built.Layout);
		Pass.Target = Target;
		std::vector<std::byte> PushData = std::move(Built.PushConstantData);
		const bool bHasPush = Built.bHasPushConstant;
		const ERHIShaderStage PushStages = Built.PushConstantStages;
		AddPass(PassType, std::move(PipelineDesc), Pass,
			[PushData = std::move(PushData), bHasPush, PushStages, PassFn = std::move(PassFn)](FRHICommandList& Cmd)
			{
				if (bHasPush && !PushData.empty())
				{
					Cmd.PushConstants(PushStages, 0, static_cast<std::uint32_t>(PushData.size()), PushData.data());
				}
				PassFn(Cmd);
			});
	}

	template <typename TParameters>
	void AddPass(
		ERHICommandListType PassType,
		FRHIGraphicsPipelineDesc PipelineDesc,
		const FRenderTarget& Target,
		const TParameters* Parameters,
		const FDrawList& DrawList)
	{
		FShaderParameterBuildResult Built = ShaderParameterBuild(*Parameters);
		FRenderPassDesc Pass;
		Pass.Layout = std::move(Built.Layout);
		Pass.Target = Target;
		AddPass(PassType, std::move(PipelineDesc), Pass, DrawList);
	}

	/**
	 * Allocate a compile-time FParameters struct (macro-declared TParameters) from
	 * FRender's resource pool and placement-new it. Maho's analogue of UE's
	 * GraphBuilder.AllocateParameters<T>(): the feature writes the returned pointer's
	 * members then hands it to AddPass. The memory is frame-transient (recycled at
	 * the next BeginFrame), so the parameter must be consumed within the allocating
	 * frame; the caller never frees it.
	 *
	 * The template only forwards to a non-template bridge (AllocParameterBytes) so
	 * this header -- which forward-declares FRHIResourcePool -- never instantiates a
	 * member template on an incomplete type (that trips an MSVC internal error).
	 */
	template <typename TParameters>
	[[nodiscard]] TParameters* AllocParameters()
	{
		void* Storage = AllocParameterBytes(sizeof(TParameters), alignof(TParameters));
		return Storage ? ::new (Storage) TParameters() : nullptr;
	}

	/** Pool-owned descriptor set layout: content-addressable get-or-create keyed by
	 *  the descriptor-set binding structure. A feature resolves the set layout its
	 *  pass binds either implicitly (the typed AddPass) or directly (e.g.
	 *  UIFeature's mirror font/set layouts). The pool owns the native lifetime
	 *  (destroyed at Shutdown); a feature holds only the handle. */
	[[nodiscard]] FRHIDescriptorSetLayout* GetOrCreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc& Desc);

	/** Pool-owned sampler: get-or-create by descriptor. The pool owns the native
	 *  (destroyed at Shutdown); a feature holds only the handle. */
	[[nodiscard]] FRHISampler* CreateSampler(const FRHISamplerDesc& Desc);

	/** Pool-owned descriptor set: content-addressable get-or-create keyed by the set
	 *  layout + referenced resources (a feature builds the FRHIDescriptorWrite array
	 *  and the pool writes it at allocation time via IRHI::UpdateDescriptorSets -- a
	 *  device-level op, not a recorded vkCmd). A feature holds only the set handle;
	 *  the pool destroys pool+set at Shutdown. */
	[[nodiscard]] FRHIDescriptorSet* GetOrCreateDescriptorSet(
		FRHIDescriptorSetLayout* Layout,
		const FRHIDescriptorSetLayoutDesc& LayoutDesc,
		const FRHIDescriptorWrite* Writes,
		std::uint32_t WriteCount);

	/** Pool-owned MUTABLE descriptor set: get-or-create by LAYOUT only, allocated
	 *  ONCE per layout (no content written at allocation). This is the single
	 *  implementation path for Static / PerFrame / PerPass pass-parameter sets --
	 *  the pass re-writes its content at record time via
	 *  FRHICommandList::UpdateDescriptorSet, so a Static set is still mutable, just
	 *  updated far less often than PerFrame / PerPass. The pool owns set+pool
	 *  (destroyed at Shutdown); a feature holds only the handle. */
	[[nodiscard]] FRHIDescriptorSet* GetOrCreateMutableDescriptorSet(
		FRHIDescriptorSetLayout* Layout,
		const FRHIDescriptorSetLayoutDesc& LayoutDesc,
		const FRHIDescriptorWrite* Writes,
		std::uint32_t WriteCount,
		bool& bOutNeedsWrite);

	/**
	 * Shader resource: async compile + explicit-sync handle. The first call per T
	 * submits the VS/FS compile to the shader server thread (CompileAsync, off the
	 * render thread) and returns a TShaderHandle<T>. The handle's Wait() blocks until
	 * that compile completes -- a feature typically calls TryGetShader in IBeginRender
	 * (before the pass) and Wait()s it, then fires its IRender pass against the ready
	 * modules. Later calls return the same cached handle (no recompile). There is NO
	 * fallback: if a feature uses the shader before Wait() the handle returns null
	 * modules, so the feature must Wait() before use.
	 *
	 * T contract (all STATIC, provided by the shader type):
	 *   static const char* GetVertexSource();     // nullptr => no VS
	 *   static const char* GetFragmentSource();   // nullptr => no FS
	 *   static const char* GetVertexEntryPoint();   // optional; default "main"
	 *   static const char* GetFragmentEntryPoint(); // optional; default "main"
	 */
	template <typename T>
	[[nodiscard]] TShaderHandle<T> TryGetShader()
	{
		auto& S = TShaderHandle<T>::GetState();
		{
			std::lock_guard Lock(S.Mutex);
			if (!S.bSubmitted && ShaderCompiler)
			{
				S.bSubmitted = true;
				// Static cache cannot be reference-captured by the callbacks (they run
				// on the shader-server thread); capture an automatic pointer instead.
				auto* PS = &S;
				if (const char* Src = T::GetVertexSource(); Src != nullptr)
				{
					FShaderCompileDesc D;
					D.Source = Src;
					D.Stage = ERHIShaderStage::Vertex;
					D.EntryPoint = Detail::GetVertexEntryPoint<T>();
					const std::string Entry = D.EntryPoint;
					ShaderCompiler->CompileAsync(D, [PS, Entry](const FShaderCompileResult& R)
					{
						std::lock_guard L(PS->Mutex);
						if (!R.bSuccess) { PS->bFailed = true; }
						else
						{
							PS->VS = R.Bytecode;
							PS->VHash = HashShaderWords(PS->VS.data(), PS->VS.size());
							PS->VEntry = Entry;
						}
						PS->bVComplete = true;
						PS->bReady = PS->bVComplete && PS->bFComplete;
					});
				}
				else { S.bVComplete = true; }

				if (const char* Src = T::GetFragmentSource(); Src != nullptr)
				{
					FShaderCompileDesc D;
					D.Source = Src;
					D.Stage = ERHIShaderStage::Fragment;
					D.EntryPoint = Detail::GetFragmentEntryPoint<T>();
					const std::string Entry = D.EntryPoint;
					ShaderCompiler->CompileAsync(D, [PS, Entry](const FShaderCompileResult& R)
					{
						std::lock_guard L(PS->Mutex);
						if (!R.bSuccess) { PS->bFailed = true; }
						else
						{
							PS->FS = R.Bytecode;
							PS->FHash = HashShaderWords(PS->FS.data(), PS->FS.size());
							PS->FEntry = Entry;
						}
						PS->bFComplete = true;
						PS->bReady = PS->bVComplete && PS->bFComplete;
					});
				}
				else { S.bFComplete = true; }
			}
		}
		return TShaderHandle<T>(this);
	}

	// -- host engine stages (FEngineBase context) --
	void PreInitialize(FEngineBase&, FEngineContext&) override;
	void Initialize(FEngineBase& Engine, FEngineContext& Frame) override;
	void PostInitialize(FEngineBase&, FEngineContext&) override;
	void PreShutdown(FEngineBase&, FEngineContext&) override;
	void Shutdown(FEngineBase& Engine, FEngineContext& Frame) override;
	void PostShutdown(FEngineBase&, FEngineContext&) override;
	void BeginFrame(FEngineBase& Engine, FEngineContext& Frame) override;
	void Tick(FEngineBase& Engine, FEngineContext& Frame) override;
	void EndFrame(FEngineBase& Engine, FEngineContext& Frame) override;
	void RequestExit(FEngineBase& Engine, FEngineContext& Frame) override;

private:
	/** TShaderHandle drives the shader compile through the pool's PSO cache; it
	 *  reaches the private GetOrCreateShaderModule / WaitShaderCompiles. */
	template <typename T> friend class TShaderHandle;

	/**
	 * PSO-resolving pass: the feature describes the pipeline config (with the VS/FS
	 * shader MODULES already resolved + filled in), the render target and the layout,
	 * and AddPass resolves the PSO (via the pool's PSO cache), starts the dynamic
	 * render pass, BINDS the graphics pipeline implicitly, then runs the draw lambda.
	 * The feature never queries a pipeline/layout and never calls BindGraphicsPipeline
	 * itself -- it only records the draws (viewport/scissor/draw).
	 *
	 * This is the PUBLIC typed FParameters template's implementation point: the
	 * compile-time path builds the FRenderPassDesc + push-constant block, then
	 * forwards here to actually record the pass. Internal: features use the
	 * macro-declared TParameters AllocParameters< > / AddPass path, never this form.
	 *
	 * `OutDefaultSets`, when given, receives the set resolved for each set index (slot =
	 * SetIndex - FirstSet) BEFORE the record lambda runs. It exists for the draw-list path, whose
	 * lambda must be able to re-bind the pass's own default sets after a per-batch set displaced
	 * them -- previously it re-looked them up by LAYOUT, which only worked while a layout had
	 * exactly one set. Optional, so no feature is affected. */
	void AddPass(
		ERHICommandListType PassType,
		FRHIGraphicsPipelineDesc PipelineDesc,
		const FRenderPassDesc& Pass,
		std::function<void(FRHICommandList&)> PassFn,
		std::vector<FRHIDescriptorSet*>* OutDefaultSets = nullptr);

	/** The async shader compiler. Internal: features reach shaders through
	 *  TryGetShader<T> only; they never touch the compiler server directly. */
	FShaderCompilerServer* GetShaderCompiler() const { return ShaderCompiler.get(); }

	/** Non-template bridge for AllocParameters<T>(): bump-allocates frame-transient
	 *  bytes from the (complete, .cpp-only) resource pool. Keeping this non-template
	 *  means the public header instantiates no member template on the forward-declared
	 *  FRHIResourcePool. */
	[[nodiscard]] void* AllocParameterBytes(std::size_t Size, std::size_t Align);

	/** Advance the RDG resource pool (expire transients + recycle the frame's
	 *  command lists) -- called by the frame host BeginFrame AFTER the swapchain
	 *  fence wait (their submits are done, no in-flight GPU references). */
	void BeginResourcePool();

	/** Block until every async shader compile submitted so far completes (the shader
	 *  server's quiescence barrier). Internal: a feature reaches this through the
	 *  TShaderHandle::Wait() -- the "sync before use" point. */
	void WaitShaderCompiles();

	/** PSO cache: get-or-create a shader module / pipeline layout / graphics
	 *  pipeline by descriptor. Internal: the typed AddPass and TShaderHandle use
	 *  these; a feature never queries a pipeline/layout directly. The pool owns the
	 *  native lifetime (destroyed at Shutdown). Shader identity is matched by
	 *  bytecode content, so a pass shares one compiled module across frames. */
	[[nodiscard]] FRHIShaderModule* GetOrCreateShaderModule(const FRHIShaderModuleDesc& Desc);
	[[nodiscard]] FRHIPipelineLayout* GetOrCreatePipelineLayout(const FRHIPipelineLayoutDesc& Desc);
	[[nodiscard]] FRHIGraphicsPipeline* GetOrCreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc);

	std::unique_ptr<FRHI> RHI;   // the render server (not a scheduled layer)
	std::unique_ptr<FShaderCompilerServer> ShaderCompiler;   // async GLSL -> SPIR-V
	std::unique_ptr<FRHIResourcePool> ResourcePool;   // RDG resource pool

	// Render graph stages. The swapchain frame lifecycle (acquire / end + present)
	// lives on the host FRender::BeginFrame/EndFrame (engine stages); the graph
	// runs the draw passes + the present blit, and FRender::EndFrame waits it
	// (Wait) before RHI->EndFrame so the present waits every submit.
	// The graph itself belongs to the collector (FFrameBuilder): the stage SEQUENCE below is
	// what FRender::Tick hands to Execute<FRenderStages>(). FRender itself does no frame work.
	// A stage no installed feature implements is not emitted at all (no empty node).
#ifdef MAHO_EDITOR_BUILD
	using FRenderStages = TTypeList<IEditorInput, IInitViews, IBeginRender, IRender, IEndRender, IPostProcess, IRenderUI, IEditorCompose, IPresent>;
#else
	using FRenderStages = TTypeList<IInitViews, IBeginRender, IRender, IEndRender, IPostProcess, IRenderUI, IPresent>;
#endif

	// Recorded passes are submitted ONCE, by the frame's IPresent stage (SubmitRecordedPasses):
	// until then a pass is just a recorded list in the frame's table, so passes record in parallel
	// and no per-pass fence exists. The frame command list's open/close stay on the host stages.

	/** Passes recorded OFF the frame graph: a pass recorded OUTSIDE any stage call (no ambient frame
	 *  context) has no frame to belong to. Today that is the asset-mirror upload, which runs on the
	 *  resource system's IO thread in the gap the graph leaves between the frame head and Tick. It
	 *  goes here and is drained FIRST by the next IPresent, so a mirror's copy is on the queue before
	 *  the draws that sample it. */
	std::mutex OffFramePassMutex;
	std::vector<FRenderContext::FPendingPass> OffFramePasses;

	/** Record one pass: acquire a list, run the lambda on THIS thread, hand the finished list back. */
	[[nodiscard]] FRHICommandList* RecordPass(const std::function<void(FRHICommandList&)>& PassFn);

	// -- CPU asset -> GPU mirror --
	/** Asset FName -> RDG mirror resource (texture or buffer). Owned by the render
	 *  resource pool; a Persistent texture/buffer stays alive until pool shutdown.
	 *  Built in OnAssetMirrorImported (mirror imported asset), released in
	 *  OnAssetMirrorUnloaded. The DESCRIPTOR-SET UI handle for each texture mirror
	 *  is owned by FUIFeature (see UIFeature::MirrorUISets); FRender only owns the
	 *  GPU mirror resource itself. */
	std::unordered_map<Name::FName, FRDGResourceRef> GpuMirrors;

	/** OUR OWN subscriptions on the resource system's events. Kept as ids so Shutdown
	 *  unbinds exactly these three -- never RemoveAll(), which would also drop handlers
	 *  other plugins registered on the same event. */
	FSubscriptionID AssetImportedSub = 0;
	FSubscriptionID AssetUnloadedSub = 0;
	FSubscriptionID AssetCreatedSub  = 0;

	/** Asset FName -> GPU sampler mirror, built from the texture's GPU sampling config
	 *  in OnAssetMirrorCreated, resolved via GetMirrorSampler. The sampler is created
	 *  through the pool (get-or-create, pool-shared); erased on OnAssetMirrorUnloaded. */
	std::unordered_map<Name::FName, FRHISampler*> GpuSamplers;

	/** The final on-screen present target for the current frame, set by a UI feature
	 *  (RenderUI) and consumed by the frame feature's IPresent. Cross-feature state --
	 *  the frame feature reads it, the UI feature that composites last writes it. */
	FRDGTextureRef PresentTarget;

	/** OnAssetImported listener: mirror the imported CPU asset to GPU (upload its
	 *  pixels), then report completion via Done so the resource system can drop the
	 *  CPU bulk. */
	void OnAssetMirrorImported(const Name::FName& AssetName, Resource::FOnTransferDone Done);

	/** OnAssetUnloaded listener: release the GPU mirror + erase the table entry. */
	void OnAssetMirrorUnloaded(const Name::FName& AssetName, Resource::FOnTransferDone Done);

	/** OnAssetCreated listener (resource system CreateResource): build a Persistent GPU
	 *  mirror from the resource's descriptor fields (no pixel upload - the resource is a
	 *  runtime placeholder) and key it by the asset FName. Render features resolve the
	 *  mirror via GetMirror(FName). Transient (per-frame) GPU resources skip this path. */
	void OnAssetMirrorCreated(const Name::FName& AssetName, const Resource::FResource& Resource);

	/** GPU fill-back (SetReadback provider): decode the GPU mirror back into the
	 *  resource's CPU fields before an export. Returns false when the resource has
	 *  no mirror or the current RHI lacks a CPU readback path. */
	[[nodiscard]] bool ReadbackMirror(const Name::FName& AssetName, Resource::FResource& OutResource);

	[[nodiscard]] static ERHIFormat FormatMirror(Resource::ETexturePixelFormat Fmt, bool bSRGB);
	[[nodiscard]] static ERHITextureDimension DimensionMirror(Resource::ETextureDimension Dim);

	/** Create a Persistent RDG texture from the asset's CPU pixels + upload it via a
	 *  transient staging buffer (one transfer submit). Stores the mirror in the table. */
	bool UploadTextureMirror(const Name::FName& AssetName, Resource::FTexture& Tex);
};

/**
 * Shader handle: an explicit-sync wrapper over a T's async compile (see
 * FRender::TryGetShader). Every instantiation of T shares ONE FShaderState (build
 * the native modules + bytecode once, reuse across frames). A feature calls Wait()
 * before use -- that is the "sync before use" point, guaranteed by flushing the
 * shader server. There is NO fallback: before Wait() the getters return null.
 *
 * The compile is driven entirely by FRender::TryGetShader (the friend); this class
 * only exposes the sync + accessors. The native module (VModule/FModule) is created
 * lazily on the calling thread through the pool's PSO cache, so a feature owns a
 * plain module pointer, not the native lifetime.
 */
template <typename T>
class TShaderHandle
{
	// FRender::TryGetShader<T> constructs and drives the per-T state.
	friend class FRender;

	explicit TShaderHandle(FRender* InOwner) : Owner(InOwner) {}

public:
	TShaderHandle() = default;
	TShaderHandle(const TShaderHandle&) = default;
	TShaderHandle& operator=(const TShaderHandle&) = default;

	/** Block until every async shader compile submitted so far completes, then
	 *  report whether the requested stages compiled successfully. A feature must
	 *  call this before using the modules (the "sync before use" point). */
	bool Wait()
	{
		if (Owner)
		{
			Owner->WaitShaderCompiles();
		}
		auto& State = GetState();
		std::lock_guard Lock(State.Mutex);
		return State.bReady && !State.bFailed;
	}

	/** True once the requested stages compiled successfully (no blocking). */
	[[nodiscard]] bool IsReady() const
	{
		auto& State = GetState();
		std::lock_guard Lock(State.Mutex);
		return State.bReady && !State.bFailed;
	}

	/** Vertex module (build once via the pool, cached). Null if no VS / not ready. */
	[[nodiscard]] FRHIShaderModule* GetVertex()
	{
		auto& State = GetState();
		std::lock_guard Lock(State.Mutex);
		if (State.VS.empty())
		{
			return nullptr;
		}
		if (!State.VModule)
		{
			FRHIShaderModuleDesc D;
			D.Stage = ERHIShaderStage::Vertex;
			D.Bytecode = State.VS.data();
			D.BytecodeSize = State.VS.size() * sizeof(std::uint32_t);
			D.EntryPoint = State.VEntry.c_str();
			State.VModule = Owner ? Owner->GetOrCreateShaderModule(D) : nullptr;
		}
		return State.VModule;
	}

	/** Fragment module (build once via the pool, cached). Null if no FS / not ready. */
	[[nodiscard]] FRHIShaderModule* GetFragment()
	{
		auto& State = GetState();
		std::lock_guard Lock(State.Mutex);
		if (State.FS.empty())
		{
			return nullptr;
		}
		if (!State.FModule)
		{
			FRHIShaderModuleDesc D;
			D.Stage = ERHIShaderStage::Fragment;
			D.Bytecode = State.FS.data();
			D.BytecodeSize = State.FS.size() * sizeof(std::uint32_t);
			D.EntryPoint = State.FEntry.c_str();
			State.FModule = Owner ? Owner->GetOrCreateShaderModule(D) : nullptr;
		}
		return State.FModule;
	}

	/** Content hash of the compiled vertex stage (0 if none / not ready). */
	[[nodiscard]] std::uint64_t GetVertexHash() const
	{
		auto& State = GetState();
		std::lock_guard Lock(State.Mutex);
		return State.VHash;
	}

	/** Content hash of the compiled fragment stage (0 if none / not ready). */
	[[nodiscard]] std::uint64_t GetFragmentHash() const
	{
		auto& State = GetState();
		std::lock_guard Lock(State.Mutex);
		return State.FHash;
	}

private:
	/** Per-T compile/ready state (one per instantiation, shared by all handles). */
	struct FShaderState
	{
		std::mutex Mutex;
		bool bSubmitted = false;
		bool bReady = false;
		bool bFailed = false;
		bool bVComplete = false;
		bool bFComplete = false;
		std::vector<std::uint32_t> VS, FS;
		std::uint64_t VHash = 0, FHash = 0;
		std::string VEntry = "main", FEntry = "main";
		FRHIShaderModule* VModule = nullptr;
		FRHIShaderModule* FModule = nullptr;
	};
	static FShaderState& GetState()
	{
		static FShaderState S;
		return S;
	}

	FRender* Owner = nullptr;
};

// Stage dispatch for the render subsystem. AFTER the class on purpose: each expansion names
// `FRender::FContext`, and a full specialization's body is compiled where it is written.
MAHO_DECLARE_STAGE_DISPATCH(FRender, IOnInstalled, IOnInstalled, OnInstalled)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IInitViews, IInitViews, InitViews)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IBeginRender, IBeginRender, BeginRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IRender,      IRender,      Render)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IEndRender,   IEndRender,   EndRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IPostProcess, IPostProcess, PostProcess)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IRenderUI,    IRenderUI,    RenderUI)
#ifdef MAHO_EDITOR_BUILD
MAHO_DECLARE_STAGE_DISPATCH(FRender, IEditorInput, IEditorInput, EditorInput)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IEditorCompose, IEditorCompose, EditorCompose)
#endif // MAHO_EDITOR_BUILD
MAHO_DECLARE_STAGE_DISPATCH(FRender, IPresent, IPresent, Present)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IPreUnInstall, IPreUnInstall, PreUnInstall)

} // namespace Maho
