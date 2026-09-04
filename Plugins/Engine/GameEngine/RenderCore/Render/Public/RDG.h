#pragma once

#include "RenderApi.h"
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>

#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace Maho
{

class FRender;
class FRHIResourcePool;

/**
 * Resource lifetime class for RDG allocations. This is the atomic allocation
 * contract that later layers (VMA aliasing, the resource-reference graph) build
 * on - keep its shape stable; every future capability below it depends on it.
 *
 * Persistent - cross-frame, stable identity (descriptor-match reuse). Native +
 * memory live until the pool shuts down; never reclaimed per frame.
 * Transient  - within-frame. Native + memory are KEPT across frames and recycled
 * (no per-frame vkCreate / vkAllocate): a same-descriptor request next frame
 * reuses them; rebuilt only when the descriptor changes. Memory aliasing of
 * non-overlapping transients is a LATER layer (see RenderResourcePool).
 */
enum class ERDGResourceLifetime : std::uint8_t
{
	Persistent,
	Transient,
};

/**
 * RDG texture handle - a non-RHI reference to a pooled off-screen texture. The
 * native FRHITexture lives in FRender's resource pool; GetRHI()/GetView() are
 * transient resolution points valid only during the current frame's recording.
 * Features never create/destroy the native object directly.
 */
class MAHO_RENDER_API FRDGTextureRef
{
public:
	FRDGTextureRef() = default;

	[[nodiscard]] bool IsValid() const
	{
		return Pool != nullptr && Id != ~0u;
	}

	void Reset()
	{
		Pool = nullptr;
		Id = ~0u;
	}

	/** Transient resolve to the pooled texture (current frame only). */
	[[nodiscard]] FRHITexture* GetRHI() const;

	/** Transient resolve to the pooled texture view (current frame only). */
	[[nodiscard]] FRHITextureView* GetView() const;

	// Render-target metadata (the descriptor the texture was created from). These
	// let a feature resolve a target's format / size WITHOUT reaching into the
	// swapchain or the RHI: the pool returns the cached descriptor verbatim.
	[[nodiscard]] ERHIFormat GetFormat() const;
	[[nodiscard]] std::uint32_t GetWidth() const;
	[[nodiscard]] std::uint32_t GetHeight() const;

private:
	friend class FRHIResourcePool;

	FRDGTextureRef(FRHIResourcePool* InPool, std::uint32_t InId)
		: Pool(InPool)
		, Id(InId)
	{
	}

	FRHIResourcePool* Pool = nullptr;
	std::uint32_t Id = ~0u;
};

/**
 * RDG buffer handle - non-RHI reference to a pooled GPU buffer. Same transient
 * resolution semantics as FRDGTextureRef.
 */
class MAHO_RENDER_API FRDGBufferRef
{
public:
	FRDGBufferRef() = default;

	[[nodiscard]] bool IsValid() const
	{
		return Pool != nullptr && Id != ~0u;
	}

	void Reset()
	{
		Pool = nullptr;
		Id = ~0u;
	}

	[[nodiscard]] FRHIBuffer* GetRHI() const;

private:
	friend class FRHIResourcePool;

	FRDGBufferRef(FRHIResourcePool* InPool, std::uint32_t InId)
		: Pool(InPool)
		, Id(InId)
	{
	}

	FRHIResourcePool* Pool = nullptr;
	std::uint32_t Id = ~0u;
};

/**
 * Virtual output target - a user declaration of where to render. Attachments are
 * described with FRDGTextureRef (off-screen) so the feature never sees a native
 * framebuffer / swapchain. FRender resolves the declaration into a concrete
 * render pass + framebuffer (cached) at BeginRenderPass time. A feature fills it
 * (AddColor/SetDepth/SetSize) and hands it to FRender::AddPass as one unit.
 */
struct MAHO_RENDER_API FRenderTarget
{
	struct MAHO_RENDER_API FAttachment
	{
		FRDGTextureRef View;
		ERHILoadOp LoadOp = ERHILoadOp::Clear;
		ERHIStoreOp StoreOp = ERHIStoreOp::Store;
		float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	};

	std::vector<FAttachment> Color;
	FAttachment Depth;
	bool bHasDepth = false;
	std::uint32_t SampleCount = 1;
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;

	void AddColor(const FAttachment& Attach) { Color.push_back(Attach); }
	void SetDepth(const FAttachment& Attach) { Depth = Attach; bHasDepth = true; }
	void SetSize(std::uint32_t InWidth, std::uint32_t InHeight) { Width = InWidth; Height = InHeight; }
};

/** Render-side GPU mirror / pass INPUT resource reference: a pooled texture OR
 *  buffer. The variant lets a pass input binding reference either; the pool
 *  resolves the active one. Defined here (not Render.h) so FPassParameter, the
 *  pass INPUT half, can reference it without a header cycle. */
using FRDGResourceRef = std::variant<FRDGTextureRef, FRDGBufferRef>;

/**
 * One descriptor binding inside a pass parameter: the shader-facing input slot
 * (binding index, type) plus the value the user "binds" -- an RDG resource ref
 * (texture / buffer). The sampler is NOT here: it is a distinct shared input in
 * FRDGDescriptorSet::Samplers, referenced by SamplerIndex. This is the UE-style
 * split -- a resource binding describes only the resource, so one sampler is
 * shared across many bindings. The user NEVER touches a FRHIDescriptorSet*: the
 * system builds a layout hash from the binding types, resolves the ref to a
 * native view/buffer, writes the set and binds it inside AddPass.
 */
struct MAHO_RENDER_API FRDGBinding
{
	ERHIDescriptorType Type = ERHIDescriptorType::CombinedImageSampler;
	ERHIShaderStage Stages = ERHIShaderStage::Fragment;
	FRDGResourceRef Resource;          // texture / buffer the user binds
	std::int32_t SamplerIndex = -1;    // index into FRDGDescriptorSet::Samplers; -1 = none
	std::uint64_t Offset = 0;          // buffer / uniform range
	std::uint64_t Range = 0;
};

/** Update granularity / lifetime of a descriptor set's CONTENT. This is a
 *  SEMANTIC invariant of the resource identity (set index, binding), decoupled
 *  from any backend materialisation strategy -- the current descriptor-set path
 *  and a future bindless path both honour it. It lives at SET granularity (never
 *  per-binding) because GLSL fixes the set index at the declared set, so the
 *  bind/rewrite unit is the whole set.
 *
 *  - Static:    per-scene, changes rarely ("变化最小"), but STILL a mutable set --
 *               every pass parameter is allowed to change, Static is just updated
 *               far less often than the others. Default.
 *  - PerFrame:  content changes every frame (per-frame GPU scene). A persistent
 *               mutable set updated via Cmd.UpdateDescriptorSet at record time,
 *               once per frame.
 *  - PerPass:   content differs by pass (basepass vs postprocess). Same mutable
 *               mechanism, updated each pass that binds it.
 *  - PerInstance: content differs per mesh batch. Requires either push-descriptor
 *               or dynamic-offset UBO; NOT expressible with a single mutable set
 *               (it holds one content, so all batches would read the last update).
 *               Bound per draw. */
enum class EDescriptorSetFrequency
{
	Static = 0,
	PerFrame,
	PerPass,
	PerInstance,
};

/** One descriptor set inside a pass parameter: set index + its bindings
 *  (binding index -> value) + the set's sampler pool. The system fills set
 *  layouts from these. */
struct MAHO_RENDER_API FRDGDescriptorSet
{
	std::uint32_t SetIndex = 0;
	/** Update granularity / lifetime of this set's content. Drives how the pass
	 *  materialises it: Static -> content-addressable pool; PerFrame / PerPass ->
	 *  persistent mutable set updated at record time; PerInstance -> bound per
	 *  batch (dynamic-offset / push descriptor). Defaults to Static, so existing
	 *  passes that never set it keep the legacy content-pool behaviour. */
	EDescriptorSetFrequency Frequency = EDescriptorSetFrequency::Static;
	/** Pool-owned sampler parameters (get-or), shared across the
	 *  set's bindings via SamplerIndex. Distinct from resources, so a single
	 *  FRHISampler* (same descriptor) is reused by many bindings. */
	std::vector<FRHISampler*> Samplers;
	std::vector<std::pair<std::uint32_t, FRDGBinding>> Bindings;

	std::int32_t AddSampler(FRHISampler* Sampler)
	{
		Samplers.push_back(Sampler);
		return static_cast<std::int32_t>(Samplers.size() - 1);
	}
};

/**
 * PassParameter -- the INPUT half wrap of a render pass, the mirror of
 * FRenderTarget (which wraps the OUTPUT: attachments + extent). The user declares
 * the descriptor bindings (per set) + push constants; the feature never sees a
 * layout or a descriptor set. A unique feature key derived from the binding
 * structure resolves to exactly ONE cached descriptor-set layout in the pool
 * (get-or-create). AddPass then materialises each set (get-or-create by the
 * referenced resources), writes the resolved native views into it (device-level
 * UpdateDescriptorSets) and binds it -- all hidden from the user.
 */
struct MAHO_RENDER_API FPassParameter
{
	std::vector<FRDGDescriptorSet> Sets;
	std::vector<FRHIPushConstantRange> PushConstants;

	FRDGDescriptorSet& AddSet(std::uint32_t SetIndex)
	{
		Sets.push_back({ SetIndex, {} });
		return Sets.back();
	}

	/** Add (or find) a binding in SetIndex; returns the value slot to fill. */
	FRDGBinding& Bind(std::uint32_t SetIndex, std::uint32_t Binding, ERHIDescriptorType Type)
	{
		for (auto& S : Sets)
		{
			if (S.SetIndex == SetIndex)
			{
				S.Bindings.push_back({ Binding, { Type } });
				return S.Bindings.back().second;
			}
		}
		FRDGDescriptorSet& S = AddSet(SetIndex);
		S.Bindings.push_back({ Binding, { Type } });
		return S.Bindings.back().second;
	}

	/** Add a sampler to SetIndex; returns its index (to set on a binding's
	 *  SamplerIndex). Samplers are pool-owned and shared: the same index may be
	 *  referenced by many bindings. */
	std::int32_t AddSampler(std::uint32_t SetIndex, FRHISampler* Sampler)
	{
		for (auto& S : Sets)
		{
			if (S.SetIndex == SetIndex)
			{
				return S.AddSampler(Sampler);
			}
		}
		return AddSet(SetIndex).AddSampler(Sampler);
	}

	/** Convenience: add a CombinedImageSampler-style binding that references a
	 *  newly stacked sampler. Equivalent to Bind(...) + AddSampler + SamplerIndex. */
	FRDGBinding& Bind(std::uint32_t SetIndex, std::uint32_t Binding, ERHIDescriptorType Type, FRHISampler* Sampler)
	{
		FRDGBinding& B = Bind(SetIndex, Binding, Type);
		B.SamplerIndex = AddSampler(SetIndex, Sampler);
		return B;
	}

	void AddPushConstants(const FRHIPushConstantRange& R) { PushConstants.push_back(R); }
};

/**
 * The complete declaration of one render pass, given to FRender::AddPass as a
 * single unit: the INPUT resource binding (FPassParameter, the descriptor-layout
 * wrap: sets + push constants) PLUS the OUTPUT where to render (FRenderTarget,
 * the framebuffer wrap: attachments + extent). Input and Output run in OPPOSITE
 * directions (in vs out), so they are grouped side-by-side as a pass
 * declaration -- never merged into the output target.
 */
struct MAHO_RENDER_API FRenderPassDesc
{
	FPassParameter Layout;       // input: descriptor sets / push constants (feature-keyed to one pool layout)
	FRenderTarget Target;        // output: attachments + extent to render into
};

} // namespace Maho
