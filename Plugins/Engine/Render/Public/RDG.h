#pragma once

#include "RenderApi.h"
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>

#include <cstdint>
#include <vector>

namespace Maho
{

class FRender;
class FRenderResourcePool;

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
	friend class FRenderResourcePool;

	FRDGTextureRef(FRenderResourcePool* InPool, std::uint32_t InId)
		: Pool(InPool)
		, Id(InId)
	{
	}

	FRenderResourcePool* Pool = nullptr;
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
	friend class FRenderResourcePool;

	FRDGBufferRef(FRenderResourcePool* InPool, std::uint32_t InId)
		: Pool(InPool)
		, Id(InId)
	{
	}

	FRenderResourcePool* Pool = nullptr;
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

/**
 * The complete declaration of one render pass, given to FRender::AddPass as a
 * single unit: the INPUT resource binding (pipeline layout: descriptor sets /
 * push constants the pass binds) PLUS the OUTPUT where to render (render target:
 * attachments + extent). Layout and Target run in OPPOSITE directions (in vs out),
 * so they are grouped side-by-side as a pass declaration -- never merged into the
 * output target.
 */
struct MAHO_RENDER_API FRenderPassDesc
{
	FRHIPipelineLayoutDesc Layout;   // input: descriptor sets / push constants
	FRenderTarget Target;            // output: attachments + extent to render into
};

} // namespace Maho
