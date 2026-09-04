#pragma once

#include "RDG.h"

#include <RHI/RHIServer.h>

#include <mutex>
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
 *   - Transient: identity = descriptor within a frame. Same-Persistent reuse
 *     during a frame, BUT at BeginFrame every transient slot is recycled
 *     (inactive) WITHOUT destroying native + memory, so next frame's
 *     same-descriptor request reuses them. The native is rebuilt ONLY when the
 *     descriptor changes (a recycled slot with a different desc is dropped and a
 *     fresh native is created). Recycling happens ONLY after the host waited the
 *     previous frame's fence, so no in-flight command references them.
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
	 * tracks it for deferred destruction: the feature records + submits in its own
	 * stages, but the list is NOT destroyed until the NEXT BeginFrame (after the
	 * host waited the previous frame's swapchain fence). This is the same
	 * fence-aligned lifetime rule as transient resources - a list that was
	 * submitted may still be executing on the GPU.
	 */
	[[nodiscard]] FRHICommandList* AcquireRenderList();

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

	/** Create (get-or-create by layout) a MUTABLE descriptor set the pool OWNS.
	 *  This is the SINGLE implementation path for Static / PerFrame / PerPass
	 *  pass-parameter sets: the set is allocated ONCE per layout and re-written at
	 *  record time by the pass (FRHICommandList::UpdateDescriptorSet) whenever its
	 *  content changes. The pool allocates the pool+set but does NOT write content
	 *  at creation; the pass owns the write. The frequency only governs how often
	 *  content changes -- never the mechanism. The caller must not read the set
	 *  from an in-flight submit when it updates it (the frame's BeginFrame fence
	 *  wait guards the single-frame graph this engine runs today; a frames-in-flight
	 *  ring is the later upgrade). Returns a borrowed handle; pool destroys
	 *  pool+set at Shutdown. */
	[[nodiscard]] FRHIDescriptorSet* GetOrCreateMutableDescriptorSet(
		FRHIDescriptorSetLayout* Layout,
		const FRHIDescriptorSetLayoutDesc& LayoutDesc);

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

private:
	struct FTextureEntry
	{
		FRHITextureDesc Desc;
		FRHITexture* Native = nullptr;
		FRHITextureView* View = nullptr;
		ERDGResourceLifetime Lifetime = ERDGResourceLifetime::Persistent;
		std::uint32_t RefCount = 0;
		bool bActive = false;
	};

	struct FBufferEntry
	{
		FRHIBufferDesc Desc;
		FRHIBuffer* Native = nullptr;
		ERDGResourceLifetime Lifetime = ERDGResourceLifetime::Persistent;
		std::uint32_t RefCount = 0;
		bool bActive = false;
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
	};

	/** Reuse an inactive slot with a matching descriptor + lifetime class, else -1. */
	[[nodiscard]] std::int32_t FindReusableTexture(const FRHITextureDesc& Desc, ERDGResourceLifetime Lifetime) const;
	[[nodiscard]] std::int32_t FindReusableBuffer(const FRHIBufferDesc& Desc, ERDGResourceLifetime Lifetime) const;

	[[nodiscard]] std::uint32_t AllocTextureSlot();
	[[nodiscard]] std::uint32_t AllocBufferSlot();

	/** Drop a slot's native + view (used when a recycled slot's descriptor changed). */
	void DestroyTextureEntry(FTextureEntry& Entry);
	void DestroyBufferEntry(FBufferEntry& Entry);

	IRHI* RHI = nullptr;
	std::vector<FTextureEntry> Textures;
	std::vector<std::uint32_t> FreeTextureSlots;
	std::vector<FBufferEntry> Buffers;
	std::vector<std::uint32_t> FreeBufferSlots;
	std::vector<FRHICommandList*> PendingRenderLists;   // lists acquired this frame; destroyed at the next BeginFrame
	std::mutex RenderListsMutex;                        // guards PendingRenderLists (features acquire on pool workers)

	std::vector<FPipelineLayoutEntry> PipelineLayouts;  // PSO cache: layout native keyed by its desc
	std::vector<FDescriptorSetLayoutEntry> DescriptorSetLayouts;  // PSO cache: set layouts (pipeline-layout deps)
	std::vector<FShaderModuleEntry> ShaderModules;           // PSO cache: shader module keyed by bytecode content
	std::vector<FGraphicsPipelineEntry> GraphicsPipelines;   // PSO cache: graphics pipeline keyed by its desc
	std::vector<FSamplerEntry> Samplers;                // pool-owned samplers (get-or-create by desc)
	std::vector<FDescriptorSetEntry> DescriptorSets;    // pool-owned descriptor pools + sets (content-addressable)
	std::vector<FDescriptorSetEntry> MutableDescriptorSets; // pool-owned mutable sets keyed by layout (written at record time)
};

} // namespace Maho
