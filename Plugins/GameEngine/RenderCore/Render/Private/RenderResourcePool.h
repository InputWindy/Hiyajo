#pragma once

#include "RDG.h"

#include <RHI/RHIServer.h>

#include <cstddef>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace Maho
{

/**
 * RDG resource pool - owns the native FRHITexture / FRHIBuffer behind FRDG*Ref
 * handles. This is the ATOMIC allocation layer: it decides WHEN a native object
 * (and its memory) is created, reused, or released, purely from the lifetime
 * class + descriptor + reference count. It knows nothing about passes, layout
 * transitions, or frame processing order - those layers sit on top.
 *
 * Allocation contract (stable - later layers build on it):
 *   - Persistent: identity = descriptor. First request creates the native on a
 *     slot; while refcount > 0 the slot is active. When refcount drops to 0 the
 *     slot goes inactive but its native + memory are KEPT (reused by a later
 *     same-descriptor request) until the pool Shutdown. Descriptor mismatch is
 *     never reclaimed (it is a long-lived target).
 *   - Transient: identity = descriptor within a FRAME, and the frame is the hard
 *     lifetime. At BeginFrame EVERY transient slot is recycled: it goes inactive and
 *     back on the free list, its refcount is zeroed, and its Generation advances --
 *     while native + memory are KEPT, so the next frame's same-descriptor request
 *     reuses them in place (that is the "no per-frame vkCreate / vkAllocate" step).
 *     The native is rebuilt only when the descriptor changed (a recycled slot with a
 *     different desc is condemned and a fresh native created). Recycling happens ONLY
 *     after the host waited the previous frame's fence, so no in-flight command
 *     references them.
 *     A handle minted in an earlier frame no longer resolves: it carries the
 *     generation it was minted with, Get*() finds a different one and REPORTS the
 *     expired handle instead of returning a native. Crossing a frame boundary with a
 *     live transient handle is therefore a detected error, never a silent alias --
 *     which is exactly why the pool can recycle unconditionally (an earlier
 *     "refcount == 0" gate could not: a handle nobody ever released kept its slot
 *     out of circulation forever, so every frame allocated a fresh slot + native).
 *
 * This is the "no per-frame vkCreate/vkAllocate" step. The NEXT layer (VMA
 * aliasing of non-overlapping transients) will need size/alignment +
 * reference-graph, and will split native creation from memory binding - neither
 * is exposed here yet.
 */
class FRHIResourcePool
{
public:
	explicit FRHIResourcePool(IRHI* InRHI)
		: RHI(InRHI)
	{
	}

	FRDGTextureRef CreateTexture(const FRHITextureDesc& Desc, ERDGResourceLifetime Lifetime);
	FRDGBufferRef CreateBuffer(const FRHIBufferDesc& Desc, ERDGResourceLifetime Lifetime);
	void ReleaseTexture(FRDGTextureRef& Ref);
	void ReleaseBuffer(FRDGBufferRef& Ref);

	/**
	 * Allocate a fresh graphics command list for a render-feature pass. The pool
	 * tracks it: a pass is recorded in its own frame and SUBMITTED at that frame's
	 * IPresent stage, and the list is destroyed at the next BeginFrame -- after the
	 * host waited the swapchain fence, so nothing is still executing. Submission is
	 * the handover point (RetireRenderLists): a list recorded BEFORE the frame loop
	 * started (an asset-mirror upload, an init-time upload) sits in a frame's table
	 * until that frame's IPresent submits it, and must survive until then.
	 */
	[[nodiscard]] FRHICommandList* AcquireRenderList();

	/** Hand back the lists this frame just SUBMITTED: they are destroyed at the next BeginFrame.
	 *  Called by the frame's submission point, so a list's lifetime starts when it is recorded and
	 *  ends one frame-boundary after it reached the queue -- never while it is still queued to be
	 *  submitted. */
	void RetireRenderLists(const std::vector<FRHICommandList*>& Lists);

	/**
	 * PSO cache: get-or-create a pipeline layout / graphics pipeline keyed by its
	 * descriptor. A feature builds a descriptor (a layout desc, or a full pipeline
	 * desc carrying shader-bytecode fingerprints), the pool returns the matching
	 * native if one is already alive, otherwise creates + caches it. The pool owns
	 * the native lifetime (destroyed at Shutdown) -- the feature only holds the
	 * handle, never the raw RHI pointer. This is the "no per-feature pipeline
	 * recompilation" step: two passes built from the same bytes + state share one
	 * native. (A linear scan is fine at current counts -- a handful per pool; a
	 * hashed index is the obvious later upgrade.)
	 */
	[[nodiscard]] FRHIPipelineLayout* GetOrCreatePipelineLayout(const FRHIPipelineLayoutDesc& Desc);
	[[nodiscard]] FRHIDescriptorSetLayout* GetOrCreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc& Desc);
	[[nodiscard]] FRHIGraphicsPipeline* GetOrCreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc);
	[[nodiscard]] FRHIShaderModule* GetOrCreateShaderModule(const FRHIShaderModuleDesc& Desc);

	/** Create (get-or-create by descriptor) a sampler the pool OWNS; the feature holds
	 *  a borrowed handle. Mirrors CreateTexture/CreateBuffer: native lifetime is the
	 *  pool's (destroyed at Shutdown), and identical descriptors share one native. */
	[[nodiscard]] FRHISampler* CreateSampler(const FRHISamplerDesc& Desc);

	/** Create (get-or-create by layout + referenced resources) a descriptor set the
	 *  pool OWNS. On a miss the pool allocates a pool sized from the layout's
	 *  bindings, allocates a set, writes its contents via IRHI::UpdateDescriptorSets
	 *  (device-level, not a recorded vkCmd), and records the referenced resources
	 *  for dependency tracking. Returns a borrowed handle; the pool destroys
	 *  pool+set at Shutdown. Content-addressable: identical (layout, writes) share
	 *  one set. */
	[[nodiscard]] FRHIDescriptorSet* GetOrCreateDescriptorSet(
		FRHIDescriptorSetLayout* Layout,
		const FRHIDescriptorSetLayoutDesc& LayoutDesc,
		const FRHIDescriptorWrite* Writes,
		std::uint32_t WriteCount);

	/** Get-or-create the MUTABLE set a pass will bind, keyed by LAYOUT **+ CONTENT**.
	 *
	 *  Content is part of the key on purpose. Keying by layout alone (what this used to do) meant
	 *  every pass sharing a layout shared ONE set whose contents were rewritten at record time --
	 *  which is exactly why recording had to be serialized against in-flight submits. With content
	 *  in the key:
	 *    - two passes with identical bindings SHARE one set (free reuse),
	 *    - different bindings get separate sets (no overwrite),
	 *    - a set whose content is already right is NEVER written again, so a set an in-flight submit
	 *      may still be reading is never touched -- which is what lets passes record in parallel.
	 *
	 *  `bOutNeedsWrite` says whether THIS caller must write the contents at record time
	 *  (`FRHICommandList::UpdateDescriptorSet`); false means the set is already correct and must be
	 *  left alone. Returns a borrowed handle; the pool owns the pool+set. */
	[[nodiscard]] FRHIDescriptorSet* GetOrCreateMutableDescriptorSet(
		FRHIDescriptorSetLayout* Layout,
		const FRHIDescriptorSetLayoutDesc& LayoutDesc,
		const FRHIDescriptorWrite* Writes,
		std::uint32_t WriteCount,
		bool& bOutNeedsWrite);

	[[nodiscard]] FRHITexture* GetTexture(const FRDGTextureRef& Ref) const;
	[[nodiscard]] FRHITextureView* GetTextureView(const FRDGTextureRef& Ref);
	/** The descriptor the texture was created from (format/extent metadata for a
	 *  feature to resolve a render target without reaching into the RHI/swapchain). */
	[[nodiscard]] const FRHITextureDesc& GetTextureDesc(const FRDGTextureRef& Ref) const;
	[[nodiscard]] FRHIBuffer* GetBuffer(const FRDGBufferRef& Ref) const;

	/** Recycle all transient slots (keep native + memory; never per-frame destroy)
	 *  AND destroy the previous frame's submitted command lists. Called at the
	 *  start of each frame AFTER the host waited the previous fence. */
	void BeginFrame();

	/** Destroy all native resources (shutdown). */
	void Shutdown();

	/**
	 * Allocate a compile-time FParameters struct (macro-declared TParameters) from
	 * the pool's frame-transient allocator and placement-new it. Returns a pointer
	 * valid for the CURRENT frame only: the bump allocator is reset at the next
	 * BeginFrame, so a feature must consume the parameter (fill it + hand it to
	 * AddPass) within the frame that allocated it. This is Maho's analogue of UE's
	 * GraphBuilder.AllocateParameters<T>() -- the caller never manages the memory,
	 * and the frame boundary is the lifetime.
	 */
	template <typename TParameters>
	[[nodiscard]] TParameters* AllocParameters()
	{
		void* Storage = AllocateFrameTransient(sizeof(TParameters), alignof(TParameters));
		return ::new (Storage) TParameters();
	}

	/** Bump-allocate Size bytes aligned to Align from the frame-transient pool.
	 *  Grows the pool (never frees) when the current chunk cannot fit the request;
	 *  every chunk is recycled (its used-high-water reset) at the next BeginFrame.
	 *  FRender::AllocParameters<T>() forwards here through a non-template bridge so
	 *  the public render header (which only forward-declares the pool) never sees a
	 *  member template on an incomplete type. */
	[[nodiscard]] void* AllocateFrameTransient(std::size_t Size, std::size_t Align);

private:
	struct FTextureEntry
	{
		FRHITextureDesc Desc;
		FRHITexture* Native = nullptr;
		FRHITextureView* View = nullptr;
		ERDGResourceLifetime Lifetime = ERDGResourceLifetime::Persistent;
		std::uint32_t RefCount = 0;
		bool bActive = false;
		/** Occupant generation: advanced every time a TRANSIENT slot is recycled at the frame
		 *  boundary. A handle carries the generation it was minted with, so a handle that outlives
		 *  its frame fails to resolve (Get*) instead of silently aliasing the next occupant.
		 *  Persistent slots never advance it (their handles are valid until the pool shuts down). */
		std::uint32_t Generation = 0;
	};

	struct FBufferEntry
	{
		FRHIBufferDesc Desc;
		FRHIBuffer* Native = nullptr;
		ERDGResourceLifetime Lifetime = ERDGResourceLifetime::Persistent;
		std::uint32_t RefCount = 0;
		bool bActive = false;
		/** See FTextureEntry::Generation. */
		std::uint32_t Generation = 0;
	};

	// PSO cache entries: the descriptor that produced the native. Reuse = find an
	// entry whose descriptor equals the request; keep the descriptor so a later
	// same-descriptor request matches (and so the pool re-reads it on Shutdown).
	struct FPipelineLayoutEntry
	{
		FRHIPipelineLayoutDesc Desc;
		FRHIPipelineLayout* Native = nullptr;
	};

	struct FGraphicsPipelineEntry
	{
		FRHIGraphicsPipelineDesc Desc;
		FRHIGraphicsPipeline* Native = nullptr;
	};

	// Descriptor set layout is a pipeline-layout dependency, so it is cached too:
	// a pipeline layout native references its set layouts, and the pool Shutdown
	// must destroy pipelines -> pipeline layouts -> descriptor set layouts (a set
	// layout must outlive the pipeline layout that referenced it).
	struct FDescriptorSetLayoutEntry
	{
		FRHIDescriptorSetLayoutDesc Desc;
		FRHIDescriptorSetLayout* Native = nullptr;
	};

	// Shader module is a pipeline-input dependency, so it is cached too: a graphics
	// pipeline native references its vertex/fragment modules, and the pool Shutdown
	// must destroy pipelines -> shader modules -> pipeline layouts -> descriptor set
	// layouts. It is keyed by CONTENT (a bytecode copy + stage + entry point), not
	// by pointer identity -- modules are rebuilt from identical bytes across passes
	// and must share one native.
	struct FShaderModuleEntry
	{
		ERHIShaderStage Stage;
		std::string EntryPoint;
		std::vector<std::uint32_t> Bytecode;
		FRHIShaderModule* Native = nullptr;
	};

	// Sampler + descriptor-set entries are pool-OWNED like textures/buffers: a
	// feature holds a borrowed handle, the pool destroys the native at Shutdown.
	struct FSamplerEntry
	{
		FRHISamplerDesc Desc;
		FRHISampler* Native = nullptr;
	};

	struct FDescriptorSetEntry
	{
		FRHIDescriptorPool* Pool = nullptr;
		FRHIDescriptorSet* Set = nullptr;
		FRHIDescriptorSetLayout* Layout = nullptr;
		std::vector<FRHIDescriptorWrite> Writes;   // content-address key + referenced-resource record
		std::uint32_t LastUsedFrame = 0;           // eviction key: the last frame that asked for this content
	};

	/** Reuse an inactive slot with a matching descriptor + lifetime class, else -1. */
	[[nodiscard]] std::int32_t FindReusableTexture(const FRHITextureDesc& Desc, ERDGResourceLifetime Lifetime) const;
	[[nodiscard]] std::int32_t FindReusableBuffer(const FRHIBufferDesc& Desc, ERDGResourceLifetime Lifetime) const;

	[[nodiscard]] std::uint32_t AllocTextureSlot();
	[[nodiscard]] std::uint32_t AllocBufferSlot();

	/** Drop a slot's native + view (used when a recycled slot's descriptor changed). */
	void DestroyTextureEntry(FTextureEntry& Entry);
	void DestroyBufferEntry(FBufferEntry& Entry);

	/** Same, but the natives are held until the NEXT frame boundary instead of being destroyed
	 *  now. A frame's resources are recycled at the frame's HEAD, while its command lists are only
	 *  submitted by that frame's TAIL -- so a native whose slot is re-used here may still be bound
	 *  in a list that has not reached the queue yet, and Vulkan reports it as a buffer destroyed
	 *  while bound in a command buffer being submitted. One frame of grace is far more than the tail
	 *  needs (it runs within the same frame), and it costs one frame of memory for a slot that
	 *  changed descriptor. */
	void CondemnTexture(FRHITexture* Native, FRHITextureView* View);
	void CondemnBuffer(FRHIBuffer* Native);
	void FlushCondemned();

	IRHI* RHI = nullptr;
	std::vector<FTextureEntry> Textures;
	std::vector<std::uint32_t> FreeTextureSlots;
	std::vector<FBufferEntry> Buffers;
	std::vector<std::uint32_t> FreeBufferSlots;
	std::vector<FRHICommandList*> PendingRenderLists;    // acquired, NOT yet submitted (destroyed at Shutdown)
	std::vector<FRHICommandList*> RetiringRenderLists;   // submitted, destroyed at the next BeginFrame
	std::mutex RenderListsMutex;                        // guards both lists (features record on pool workers)

	std::vector<FPipelineLayoutEntry> PipelineLayouts;  // PSO cache: layout native keyed by its desc
	std::vector<FDescriptorSetLayoutEntry> DescriptorSetLayouts;  // PSO cache: set layouts (pipeline-layout deps)
	std::vector<FShaderModuleEntry> ShaderModules;           // PSO cache: shader module keyed by bytecode content
	std::vector<FGraphicsPipelineEntry> GraphicsPipelines;   // PSO cache: graphics pipeline keyed by its desc
	std::vector<FSamplerEntry> Samplers;                // pool-owned samplers (get-or-create by desc)
	std::vector<FDescriptorSetEntry> DescriptorSets;    // pool-owned descriptor pools + sets (content-addressable)
	std::vector<FDescriptorSetEntry> MutableDescriptorSets; // pool-owned mutable sets keyed by layout + content

	/** Frames seen, and the age at which a descriptor set is dropped. Sets are content-addressed, so a
	 *  request whose content no longer repeats (a transient buffer's address, a resized UI buffer,
	 *  a texture that stopped being drawn) would otherwise leave its set + pool alive forever. Both
	 *  entry tables are swept at BeginFrame: an entry unused for more than the grace period is freed.
	 *
	 *  Grace is deliberately not 1: a frame's sets are referenced by the command lists IT submitted,
	 *  and BeginFrame only sweeps after it waited the frame fence. Two frames of slack keeps the
	 *  sweep clear of a set that a still-recorded (not yet submitted) list of the frame being built
	 *  would have used -- the same reason the command lists themselves are retired one boundary
	 *  later. `FrameCounter` advances in BeginFrame, so it counts the frame being built. */
	static constexpr std::uint32_t kDescriptorSetGraceFrames = 2;
	std::uint32_t FrameCounter = 0;

	// Natives whose slot was re-used with a different descriptor: destroyed a frame later (see
	// CondemnTexture / CondemnBuffer) so a submission still in flight cannot be invalidated.
	std::vector<std::pair<FRHITexture*, FRHITextureView*>> CondemnedTextures;
	std::vector<FRHIBuffer*> CondemnedBuffers;

	/** Free one entry's set + pool (the pair is pool-owned; the set is freed before its pool). */
	void DestroyDescriptorSetEntry(FDescriptorSetEntry& Entry);

	// Frame-transient parameter pool (bump allocator). Parameter structs are
	// SmallPoD-size (a handful of descriptors + push-constant scalars), allocated in
	// fixed chunks that are REUSED across frames (never freed, never realloc'd so an
	// outstanding pointer stays valid for its frame). BeginFrame resets the used
	// high-water of every chunk, recycling the whole pool for the next frame --
	// this is the frame boundary that bounds a parameter's lifetime.
	struct FFrameChunk
	{
		std::vector<std::byte> Data;
		std::size_t Used = 0;   // bump offset within Data
	};
	std::vector<FFrameChunk> FrameChunks;
	std::size_t FrameChunkCursor = 0;   // last chunk served (allocation scans forward from here)
	std::mutex FrameAllocMutex;         // guards bump allocation (features allocate from pool workers)
};

} // namespace Maho
