#pragma once

#include "RenderApi.h"
#include "RDG.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Maho
{

/**
 * Mesh batch (draw protocol unit). Describes one draw call in the form AddPass
 * consumes; AddPass never knows who produced it (today FScene's hardcoded
 * triangle, later a real scene renderer).
 *
 * Geometry source priority (first non-empty wins):
 *   1. pass-level GPU vertex buffer (FDrawList::SetVertexBuffer, a FRDGBufferRef)
 *      -- the batch is a SLICE of that one uploaded buffer, addressed by
 *      VertexOffset / IndexOffset. This is the ImGui path: one merged vertex/index
 *      array per frame, uploaded in InitViews; every draw command slices it.
 *   2. batch-owned FRDGBufferRef (VertexBuffer / IndexBuffer) -- a GPU buffer
 *      the producer already holds. The scene-triangle path.
 *   3. both empty => primitive generated in-shader from gl_VertexIndex
 *      (recorded as Draw(VertexCount), no vertex buffer).
 *
 * Per-batch descriptor bindings: Sets override the Pass-level default sets for
 * this draw ONLY. Each entry's bindings reference RDG resources (FRDGTextureRef /
 * FRDGBufferRef); AddPass resolves each per-batch set by CONTENT
 * (content-addressable get-or-create) and binds it in place of the default. This
 * is how an ImGui draw command that draws with a different texture (every
 * ImDrawCmd has its own ImTextureID) picks a different set-0 CombinedImageSampler.
 */
struct MAHO_RENDER_API FDrawBatch
{
	FRDGBufferRef VertexBuffer;   // empty => pass-level data or in-shader generated
	std::uint32_t VertexOffset = 0;
	std::uint32_t VertexCount = 3;
	std::uint32_t InstanceCount = 1;
	FRDGBufferRef IndexBuffer;    // empty => non-indexed draw
	std::uint32_t IndexOffset = 0;
	std::uint32_t IndexCount = 0;
	bool bIndex32 = true;

	/** Scissor for this draw (per-cmd clip). bHasScissor=false uses the pass's full
	 *  target rect. ImGui carries a ClipRect per ImDrawCmd -- this is the slice. */
	bool bHasScissor = false;
	std::int32_t ScissorX = 0;
	std::int32_t ScissorY = 0;
	std::uint32_t ScissorW = 0;
	std::uint32_t ScissorH = 0;

	/** Per-batch descriptor-set values (set index -> bindings). Empty => use the
	 *  pass-level sets. Each set's resource bindings are resolved by content, so a
	 *  repeated (same view/sampler) batch reuses one pooled set. */
	std::vector<FRDGDescriptorSet> Sets;
};

/**
 * Draw list (protocol): the mesh batches of ONE subpass, plus an optional
 * pass-level CPU primitive buffer. One AddPass corresponds to one subpass, so a
 * single DrawList is that subpass's draw set -- no subpass grouping key. AddPass
 * consumes it: uploads the CPU primitive data once (if any), resolves + binds the
 * per-batch descriptor sets, and records the draws. Producers (FScene, FUIEH) fill it.
 */
class MAHO_RENDER_API FDrawList
{
public:
	void Add(const FDrawBatch& Batch) { Batches.push_back(Batch); }

	/** Clear every field back to empty/zero (GPU buffer refs, push constants,
	 *  batches), KEEPING the batch vectors' capacity. Used when the list is a reused
	 *  member filled per frame, so a re-filled list does not accumulate the
	 *  previous frame's batches. The GPU buffers are transient pool resources --
	 *  Reset only drops the refs, it never frees them. */
	void Reset()
	{
		VertexBuffer = FRDGBufferRef{};
		IndexBuffer = FRDGBufferRef{};
		PushStages = ERHIShaderStage::Vertex;
		PushSize = 0;
		PushData.clear();
		Batches.clear();
	}

	[[nodiscard]] const std::vector<FDrawBatch>& GetBatches() const { return Batches; }

	/** Optional pass-level push constant (e.g. the ImGui ortho projection mat4).
	 *  The data is copied here so the producer's buffer needs no lifetime; AddPass
	 *  records PushConstants(stages, 0, size, data) before the batches. */
	void SetPushConstants(ERHIShaderStage InStages, std::uint32_t InSize, const void* InData)
	{
		PushStages = InStages;
		PushSize = InSize;
		if (InData != nullptr && InSize > 0)
		{
			const auto* const Bytes = static_cast<const std::uint8_t*>(InData);
			PushData.assign(Bytes, Bytes + InSize);
		}
		else
		{
			PushData.clear();
		}
	}
	[[nodiscard]] bool HasPushConstants() const { return !PushData.empty(); }
	[[nodiscard]] ERHIShaderStage GetPushConstantStages() const { return PushStages; }
	[[nodiscard]] std::uint32_t GetPushConstantSize() const { return PushSize; }
	[[nodiscard]] const void* GetPushConstantData() const { return PushData.data(); }

	/** Pass-level GPU primitive buffer refs: a single merged vertex/indices block
	 *  uploaded in InitViews (not at AddPass), sliced per-batch by
	 *  VertexOffset/IndexOffset. When set, batches must leave their own
	 *  VertexBuffer/IndexBuffer empty (geometry source priority 1). The producer
	 *  uploads the arrays to these transient GPU buffers and hands only the refs --
	 *  the list holds NO CPU copy. */
	void SetVertexBuffer(const FRDGBufferRef& InVertexBuffer) { VertexBuffer = InVertexBuffer; }
	void SetIndexBuffer(const FRDGBufferRef& InIndexBuffer) { IndexBuffer = InIndexBuffer; }

	[[nodiscard]] bool HasPrimitiveData() const { return VertexBuffer.IsValid(); }
	[[nodiscard]] const FRDGBufferRef& GetVertexBuffer() const { return VertexBuffer; }
	[[nodiscard]] const FRDGBufferRef& GetIndexBuffer() const { return IndexBuffer; }

private:
	FRDGBufferRef VertexBuffer;   // pass-level merged vertex buffer (transient, uploaded in InitViews)
	FRDGBufferRef IndexBuffer;    // pass-level merged index buffer (transient)

	ERHIShaderStage PushStages = ERHIShaderStage::Vertex;
	std::uint32_t PushSize = 0;
	std::vector<std::uint8_t> PushData;

	std::vector<FDrawBatch> Batches;
};

} // namespace Maho
