#pragma once

// Forward declaration of Dear ImGui's draw-data (global namespace). The RHI
// backend consumes ImDrawData*; only the pointer is stored here, so an
// incomplete type suffices. The UI feature (which includes imgui.h) provides
// the full definition.
struct ImDrawData;

#include "RenderApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Engine/LayerCollector.h>
#include <Engine/LayerTaskGraph.h>
#include <Engine/Engine.h>
#include <RHI/RHIServer.h>
#include "RDG.h"
#include "RenderDrawList.h"
#include "ShaderCompiler.h"
#include <Resource.h>
#include <AssetTypes.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Maho
{

class FRender;
class FRHIResourcePool;

template <typename T> class TShaderHandle;

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
	virtual void OnInstalled(FRender&) = 0;
};

class MAHO_RENDER_API IInitViews
{
public:
	virtual ~IInitViews() = default;
	virtual void InitViews(FRender&) = 0;
};

class MAHO_RENDER_API IBeginRender
{
public:
	virtual ~IBeginRender() = default;
	virtual void BeginRender(FRender&) = 0;
};

class MAHO_RENDER_API IRender
{
public:
	virtual ~IRender() = default;
	virtual void Render(FRender&) = 0;
};

class MAHO_RENDER_API IEndRender
{
public:
	virtual ~IEndRender() = default;
	virtual void EndRender(FRender&) = 0;
};

class MAHO_RENDER_API IPostProcess
{
public:
	virtual ~IPostProcess() = default;
	virtual void PostProcess(FRender&) = 0;
};

class MAHO_RENDER_API IRenderUI
{
public:
	virtual ~IRenderUI() = default;
	virtual void RenderUI(FRender&) = 0;
};

class MAHO_RENDER_API IPresent
{
public:
	virtual ~IPresent() = default;
	virtual void Present(FRender&) = 0;
};

class MAHO_RENDER_API IPreUnInstall
{
public:
	virtual ~IPreUnInstall() = default;
	virtual void PreUnInstall(FRender&) = 0;
};

MAHO_DECLARE_STAGE_DISPATCH(FRender, IOnInstalled, IOnInstalled, OnInstalled)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IInitViews, IInitViews, InitViews)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IBeginRender, IBeginRender, BeginRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IRender,      IRender,      Render)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IEndRender,   IEndRender,   EndRender)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IPostProcess, IPostProcess, PostProcess)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IRenderUI,    IRenderUI,    RenderUI)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IPresent, IPresent, Present)
MAHO_DECLARE_STAGE_DISPATCH(FRender, IPreUnInstall, IPreUnInstall, PreUnInstall)

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
	: public FLayer<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>
	, public FLayerCollector<FRender>
{
MAHO_DECLARE_LAYER(FRender, "Render.dll");

	FRender();
	~FRender() override;
public:
	/** The UI's CPU-side ImGui context is owned by FRender (created in Initialize,
	 *  driven across BeginFrame/Tick/Shutdown). ImGui::GetDrawData() is stored here
	 *  each Tick and consumed by the UIFeature render backend. */
	ImDrawData* UIData = nullptr;
	/** Whether the ImGui context has been created (guards BeginFrame/Tick/Shutdown). */
	bool bUIInitialized = false;

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

	/** A frame-UI builder: runs after the host ITick stage builds the ImGui frame
	 *  and BEFORE ImGui::Render() closes it, so a builder can emit extra UI (e.g. a
	 *  mirror preview window) that composites over the plugin UI. FRender owns only
	 *  the ImGui context + final draw data; UI emission is pushed here so FRender
	 *  keeps zero render-specific UI logic. */
	using FUIBuildFn = std::function<void(FRender&)>;
	void AddUIBuilder(FUIBuildFn Fn) { UIBuilders.push_back(std::move(Fn)); }
	void ClearUIBuilders() { UIBuilders.clear(); }
public:
	// -- render surface / canvas info + present. FRender owns the RHI; features
	// never reach the raw IRHI* (no GetRHI()). The canvas is the swapchain geometry
	// the scene color target is sized to, and the swapchain format it is created in.
	[[nodiscard]] std::uint32_t GetCanvasWidth() const;
	[[nodiscard]] std::uint32_t GetCanvasHeight() const;
	[[nodiscard]] ERHIFormat GetSwapchainFormat() const;
	/** Blit a scene-color RDG texture to the swapchain backbuffer (the frame feature's present point). */
	void PresentTexture(const FRDGTextureRef& Texture);

	// -- RDG resource pool (off-screen resources) --
	[[nodiscard]] FRDGTextureRef CreateTexture(const FRHITextureDesc& Desc, ERDGResourceLifetime Lifetime = ERDGResourceLifetime::Persistent);
	[[nodiscard]] FRDGBufferRef CreateBuffer(const FRHIBufferDesc& Desc, ERDGResourceLifetime Lifetime = ERDGResourceLifetime::Persistent);
	void ReleaseTexture(FRDGTextureRef& Ref);
	void ReleaseBuffer(FRDGBufferRef& Ref);

	/** Record + submit a render-feature pass in one call (syntactic sugar over
	 *  AcquireRenderList + Begin/End + Submit). The lambda receives the command list
	 *  mid-pass: record draw commands, do NOT call Begin/End/Submit yourself. The
	 *  pass submits here, so it runs at the AddPass call site (the feature's IRender
	 *  stage). Order between passes is the order features call AddPass -- guarantee
	 *  it with stage deps against the feature(s) that must submit first. */
	void AddPass(ERHICommandListType PassType, std::function<void(FRHICommandList&)> PassFn);

	/**
	 * Typed pass: the feature describes the pipeline config (with the VS/FS shader
	 * MODULES already resolved + filled in), the render target and the layout, and
	 * AddPass resolves the PSO (via the pool's PSO cache), starts the dynamic render
	 * pass, BINDS the graphics pipeline implicitly, then runs the draw lambda.
	 * The feature never queries a pipeline/layout and never calls BindGraphicsPipeline
	 * itself -- it only records the draws (viewport/scissor/draw).
	 *
	 * The feature fetches the modules itself (TryGetShader<T> + Wait + GetVertex/
	 * GetFragment) and stores them in PipelineDesc.VertexShader/FragmentShader (plus
	 * the bytecode hashes + entry points); GetOrCreateGraphicsPipeline keys on those
	 * hashes, so repeated calls return the pool-owned pipeline without rebuilding.
	 * PipelineDesc is copied, so the caller may keep a state table once.
	 */
	void AddPass(
		ERHICommandListType PassType,
		FRHIGraphicsPipelineDesc PipelineDesc,
		const FRenderPassDesc& Pass,
		std::function<void(FRHICommandList&)> PassFn);

	/**
	 * Declarative draw-list pass: consumes a FDrawList INSTEAD of a record lambda.
	 * AddPass uploads the pass-level CPU primitive data (FDrawList::SetPrimitiveData)
	 * ONCE, materialises the pass-level sets, then for each batch records: bind its
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
		const FRHIDescriptorSetLayoutDesc& LayoutDesc);

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
	void PreInitialize(FEngineBase&) override;
	void Initialize(FEngineBase& Engine) override;
	void PostInitialize(FEngineBase&) override;
	void PreShutdown(FEngineBase&) override;
	void Shutdown(FEngineBase& Engine) override;
	void PostShutdown(FEngineBase&) override;
	void BeginFrame(FEngineBase& Engine) override;
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase& Engine) override;
	void RequestExit(FEngineBase& Engine) override;

private:
	/** TShaderHandle drives the shader compile through the pool's PSO cache; it
	 *  reaches the private GetOrCreateShaderModule / WaitShaderCompiles. */
	template <typename T> friend class TShaderHandle;

	/** The async shader compiler. Internal: features reach shaders through
	 *  TryGetShader<T> only; they never touch the compiler server directly. */
	FShaderCompilerServer* GetShaderCompiler() const { return ShaderCompiler.get(); }

	/** Acquire a render list, run Record mid-pass, End + Submit. Internal: shared by
	 *  the plain AddPass and the PSO-resolving AddPass (which records
	 *  BeginRendering/Bind/End around the feature's draw lambda). Keeping this
	 *  non-template keeps FRHIResourcePool complete only in Render.cpp. */
	void SubmitPass(ERHICommandListType PassType, std::function<void(FRHICommandList&)> Record);

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
	// runs the draw passes + the present blit, and FRender::EndFrame drains it
	// (Flush) before RHI->EndFrame so the present waits every submit.
	// TaskGraph orders everything. FRender itself does no frame work.
	using FRenderStages = TTypeList<IInitViews, IBeginRender, IRender, IEndRender, IPostProcess, IRenderUI, IPresent>;
	std::unique_ptr<FLayerTaskGraph<FRenderStages, FRender>> RenderGraph;

	// -- CPU asset -> GPU mirror --
	/** Asset FName -> RDG mirror resource (texture or buffer). Owned by the render
	 *  resource pool; a Persistent texture/buffer stays alive until pool shutdown.
	 *  Built in OnAssetMirrorImported (mirror imported asset), released in
	 *  OnAssetMirrorUnloaded. The DESCRIPTOR-SET UI handle for each texture mirror
	 *  is owned by FUIFeature (see UIFeature::MirrorUISets); FRender only owns the
	 *  GPU mirror resource itself. */
	std::unordered_map<Name::FName, FRDGResourceRef> GpuMirrors;

	/** Pre-Render UI builders, registered by UI features (e.g. UIFeature) and run
	 *  each UI frame after the host ITick stage, before ImGui::Render(). This keeps
	 *  FRender free of render-specific UI logic -- it only owns the ImGui context +
	 *  the final ImDrawData. Cleared at Shutdown before the features are destroyed. */
	std::vector<FUIBuildFn> UIBuilders;


	/** OnAssetImported listener: mirror the imported CPU asset to GPU (upload its
	 *  pixels), then report completion via Done so the resource system can drop the
	 *  CPU bulk. */
	void OnAssetMirrorImported(const Name::FName& AssetName, Resource::FOnTransferDone Done);

	/** OnAssetUnloaded listener: release the GPU mirror + erase the table entry. */
	void OnAssetMirrorUnloaded(const Name::FName& AssetName, Resource::FOnTransferDone Done);

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

} // namespace Maho
