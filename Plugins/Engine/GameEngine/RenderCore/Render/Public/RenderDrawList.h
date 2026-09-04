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
 *   1. pass-level CPU primitive data (FDrawList::SetPrimitiveData) -- the batch
 *      is a SLICE of that one uploaded buffer, addressed by VertexOffset /
 *      IndexOffset. This is the ImGui path: one merged vertex/index array per
 *      frame, every draw command slices it.
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

	/** Clear every field back to empty/zero (primitive data, push constants,
	 *  batches), KEEPING the vectors' capacity. Used when the list is a reused
	 *  member filled per frame, so a re-filled list does not accumulate the
	 *  previous frame's batches. The internal primitive buffers keep capacity. */
	void Reset()
	{
		VertexBytes = 0;
		VertexStride = 0;
		VertexCount = 0;
		IndexBytes = 0;
		bIndex32 = true;
		IndexCount = 0;
		PushStages = ERHIShaderStage::Vertex;
		PushSize = 0;
		PushData.clear();
		VertexData.clear();
		IndexData.clear();
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

	/** Optional pass-level CPU primitive buffer: a single merged vertex/indices
	 *  block uploaded ONCE by AddPass, sliced per-batch by VertexOffset/IndexOffset.
	 *  When set, batches must leave their own VertexBuffer/IndexBuffer empty
	 *  (geometry source priority 1). The data is COPIED here, so the producer's
	 *  arrays (e.g. ImGui's draw data) need no lifetime beyond this call -- the
	 *  list owns the buffer and AddPass reads the owned copy. */
	void SetPrimitiveData(
		const void* InVertexData, std::uint64_t InVertexBytes, std::uint32_t InVertexStride, std::uint32_t InVertexCount,
		const void* InIndexData, std::uint64_t InIndexBytes, bool InIndex32, std::uint32_t InIndexCount)
	{
		if (InVertexData != nullptr && InVertexBytes > 0)
		{
			const auto* const Bytes = static_cast<const std::uint8_t*>(InVertexData);
			VertexData.assign(Bytes, Bytes + InVertexBytes);
		}
		else
		{
			VertexData.clear();
		}
		VertexBytes = InVertexBytes;
		VertexStride = InVertexStride;
		VertexCount = InVertexCount;
		if (InIndexData != nullptr && InIndexBytes > 0)
		{
			const auto* const Bytes = static_cast<const std::uint8_t*>(InIndexData);
			IndexData.assign(Bytes, Bytes + InIndexBytes);
		}
		else
		{
			IndexData.clear();
		}
		IndexBytes = InIndexBytes;
		bIndex32 = InIndex32;
		IndexCount = InIndexCount;
	}

	[[nodiscard]] bool HasPrimitiveData() const { return VertexBytes > 0 && !VertexData.empty(); }
	[[nodiscard]] const void* GetVertexData() const { return VertexData.empty() ? nullptr : VertexData.data(); }
	[[nodiscard]] std::uint64_t GetVertexBytes() const { return VertexBytes; }
	[[nodiscard]] std::uint32_t GetVertexStride() const { return VertexStride; }
	[[nodiscard]] std::uint32_t GetVertexCount() const { return VertexCount; }
	[[nodiscard]] const void* GetIndexData() const { return IndexData.empty() ? nullptr : IndexData.data(); }
	[[nodiscard]] std::uint64_t GetIndexBytes() const { return IndexBytes; }
	[[nodiscard]] bool GetIndex32() const { return bIndex32; }
	[[nodiscard]] std::uint32_t GetIndexCount() const { return IndexCount; }

private:
	std::vector<std::uint8_t> VertexData;
	std::uint64_t VertexBytes = 0;
	std::uint32_t VertexStride = 0;
	std::uint32_t VertexCount = 0;
	std::vector<std::uint8_t> IndexData;
	std::uint64_t IndexBytes = 0;
	bool bIndex32 = true;
	std::uint32_t IndexCount = 0;

	ERHIShaderStage PushStages = ERHIShaderStage::Vertex;
	std::uint32_t PushSize = 0;
	std::vector<std::uint8_t> PushData;

	std::vector<FDrawBatch> Batches;
};

} // namespace Maho
