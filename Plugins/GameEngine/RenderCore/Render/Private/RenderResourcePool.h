#pragma once

#include "RDG.h"

#include <Core/FrameGraph.h>   // MAHO_FRAMES_IN_FLIGHT: the frame-domain slot count (kFrameSlots)
#include <RHI/RHIServer.h>

#include <array>
#include <cstddef>
#include <mutex>
#include <new>
#include <string>
#include <utility>
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
 *   - ONE slot = at most ONE live resource per frame. A slot handed to a caller
 *     (fresh from the free list, or a REUSE of a recycled same-descriptor slot) is
 *     taken OFF the frame's free list, and Alloc*Slot never hands out a slot whose
 *     entry is still bActive. Without that, one slot could carry two occupants in a
 *     frame, the generation would not change, and the first caller's handle would
 *     silently resolve to the second caller's native (a draw binding a buffer it
 *     never asked for -- see the comment on Alloc*Slot in the .cpp).
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
	 * IPresent stage, and the list is destroyed at the BeginFrame that retires its
	 * frame's slot -- the frame head waited that slot's fence, so nothing is still
	 * executing. Submission is the handover point (RetireRenderLists): a list recorded
	 * BEFORE the frame loop started (an asset-mirror upload, an init-time upload) sits
	 * in a frame's table until that frame's IPresent submits it, and must survive
	 * until then.
	 */
	[[nodiscard]] FRHICommandList* AcquireRenderList();

	/** Hand back the lists this frame just SUBMITTED: they are destroyed when this frame's slot comes
	 *  round again (`kFrameSlots` boundaries later), which is the point the RHI's per-slot fence proves
	 *  them done. Called by the frame's submission point, so a list's lifetime starts when it is
	 *  recorded and ends at the fence that covers its submission -- never while it is still queued. */
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
		/** The frame (pool `FrameCounter`) this transient was last HANDED OUT in. A transient's native
		 *  may only be handed to another frame once the frame that used it has retired on the GPU,
		 *  which is what `kFrameLifetimeFrames = MAHO_FRAMES_IN_FLIGHT` expresses: the RHI's frame head
		 *  waits the fence of the frame K slots back, so "K frames old" IS "provably retired". With the
		 *  old unconditional per-frame recycle a transient reused at frame N could still be read by
		 *  frame N-1's queued command lists -- the same class of race as the camera ring's, and it only
		 *  stayed invisible because this frame's material parameter bytes happen to be constant. */
		std::uint32_t LastUsedFrame = 0;
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
		/** See FTextureEntry::LastUsedFrame. */
		std::uint32_t LastUsedFrame = 0;
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

	/** Same, but the natives are held until the frame that could still reference them has RETIRED
	 *  instead of being destroyed now. A frame's resources are recycled at the frame's HEAD, while its
	 *  command lists are only submitted by that frame's TAIL -- so a native whose slot is re-used here
	 *  may still be bound in a list that has not reached the queue yet, and Vulkan reports it as a
	 *  buffer destroyed while bound in a command buffer being submitted
	 *  (`VUID-vkDestroyBuffer-buffer-00922`). The hold is bucketed by FRAME SLOT: the frame head waits
	 *  the per-slot fence of the frame `kFrameSlots` back, so that -- and only that -- frame's slot
	 *  bucket is provably retired and is the one freed (see BeginFrame / FlushCondemned).
	 *
	 *  ONE frame of grace was the depth-1 reading of this (a single device fence meant the frame before
	 *  the head was done); at depth K it hands live natives to `vkDestroy*`. */
	void CondemnTexture(FRHITexture* Native, FRHITextureView* View);
	void CondemnBuffer(FRHIBuffer* Native);

	/** Free the condemned natives of ONE frame-slot bucket -- the slot whose fence the frame head just
	 *  waited. Called by BeginFrame with the slot it retires; never "everything". */
	void FlushCondemned(std::uint32_t Slot);

	/** Free EVERY bucket. Only legal when the device is idle (Shutdown), where no slot is in flight. */
	void FlushAllCondemned();

	IRHI* RHI = nullptr;

	/** Guards EVERY table below that a recorded pass may touch: the texture/buffer slots + their free
	 *  lists, the condemned natives, the PSO caches, the descriptor-set tables and FrameCounter.
	 *
	 *  Why it must exist: a feature records on a POOL WORKER, and two features whose stages share a
	 *  dependency are dispatched to two different workers AT THE SAME TIME -- `MeshFeature::IRender`,
	 *  `DrawTriangleFeature::IRender` and `UIFeature::IRenderUI` all wait only for `FScene::IEndRender`,
	 *  so they are ready in the same instant and both allocate from THIS pool. Without the lock, one
	 *  worker's `push_back`/`erase` runs inside another's `std::find(begin(), end())` +
	 *  `It != end()`: the second comparison sees an iterator the first one ORPHANED, so Release
	 *  corrupts the table silently while Debug reports `vector iterators incompatible`
	 *  (observed 2026-10-05: RenderResourcePool.cpp:215, `TakeOffFreeList` <- `CreateBuffer` <-
	 *  `FMeshFeature::Render`, the other worker being UIFeature's per-frame buffer upload).
	 *
	 *  Allocation (native creation) happens under this lock. Lock order is
	 *  TableMutex -> RenderListsMutex -> FrameAllocMutex (see BeginFrame/Shutdown); nothing takes them
	 *  in the opposite order. */
	mutable std::mutex TableMutex;

	/** Frame-state slots, ONE per frame the RHI lets overlap: `MAHO_FRAMES_IN_FLIGHT`, the same number
	 *  the device's per-slot fence ring uses (`FVulkanRHI::kFrameSlots`). The frame head (RHI
	 *  `BeginFrame`, then this pool's `BeginFrame`, in that order, once per frame) waits the fence of
	 *  ONE slot -- the slot the frame it is opening will use, signalled by the submit of the frame
	 *  `kFrameSlots` boundaries back -- so exactly one bucket per frame-domain table is retired per
	 *  boundary, and it is that slot's bucket. */
	static constexpr std::uint32_t kFrameSlots = MAHO_FRAMES_IN_FLIGHT;

	/** The RHI frame SLOT of the frame in progress -- the bucket key for every frame-domain retirement.
	 *
	 *  `FrameCounter` counts from 1 while a frame runs (it advances at the TOP of BeginFrame, before the
	 *  sweeps that need it), so the frame being built is `FrameCounter - 1` and its slot is that value
	 *  mod `kFrameSlots`, which is exactly what `FVulkanRHI::BeginFrame` used to pick the slot. Read at
	 *  the top of a frame head it is therefore also the slot whose fence was just waited: one expression
	 *  names both "the bucket to retire now" and "the bucket this frame's natives belong to".
	 *  (An off-by-one here is not theoretical: stamping a frame's work with the NEXT slot's bucket frees
	 *  it at the next frame head instead of K boundaries later -- measured as 11 in-use command buffers
	 *  freed in one sweep, `VUID-vkFreeCommandBuffers-pCommandBuffers-00047`.) */
	[[nodiscard]] std::uint32_t CurrentFrameSlot() const
	{
		return (FrameCounter + kFrameSlots - 1) % kFrameSlots;
	}

	std::vector<FTextureEntry> Textures;
	std::vector<std::uint32_t> FreeTextureSlots;
	std::vector<FBufferEntry> Buffers;
	std::vector<std::uint32_t> FreeBufferSlots;
	std::vector<FRHICommandList*> PendingRenderLists;    // acquired, NOT yet submitted (destroyed at Shutdown)

	/** Submitted lists, bucketed by the RHI frame SLOT they were submitted in. A frame head waits ONE
	 *  per-slot fence, so it learns exactly ONE thing: the frame `kFrameSlots` back has retired. Only
	 *  that frame's bucket may be destroyed; the whole-table swap this replaces destroyed the lists of
	 *  frames that were still on the queue, and Vulkan reported it as
	 *  `VUID-vkFreeCommandBuffers-pCommandBuffers-00047` ("is in use"). */
	std::vector<FRHICommandList*> RetiringRenderLists[kFrameSlots];
	std::mutex RenderListsMutex;                        // guards the list tables (features record on pool workers)

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
	 *  Grace is the IN-FLIGHT DEPTH, not 1: a frame's sets are referenced by the command lists it
	 *  submitted, and those are still on the queue while K frames overlap -- `kFrameSlots` slack is
	 *  what keeps the sweep clear of a set a still-in-flight (or still-being-recorded) frame used.
	 *  Anything less was the depth-1 reading of "the previous frame is done". */
	static constexpr std::uint32_t kDescriptorSetGraceFrames = MAHO_FRAMES_IN_FLIGHT;
	std::uint32_t FrameCounter = 0;

	// Natives whose slot was re-used with a different descriptor: held for `kFrameSlots` frames (see
	// CondemnTexture / CondemnBuffer), bucketed by the frame SLOT that condemned them so a submission
	// still in flight cannot be invalidated.
	std::array<std::vector<std::pair<FRHITexture*, FRHITextureView*>>, kFrameSlots> CondemnedTextures;
	std::array<std::vector<FRHIBuffer*>, kFrameSlots> CondemnedBuffers;

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
