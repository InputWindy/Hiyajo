#pragma once

#include "RenderApi.h"
#include "RDG.h"

#include <cstdint>
#include <vector>

namespace Maho
{

/**
 * Mesh batch (draw protocol unit). Describes one draw call in the form AddPass
 * consumes; AddPass never knows who produced it (today FScene's hardcoded
 * triangle, later a real scene renderer). Either VertexBuffer (positions read by
 * the vertex shader) or a primitive generated purely from gl_VertexIndex (both
 * buffers empty => recorded as Draw(VertexCount)).
 */
struct MAHO_RENDER_API FDrawBatch
{
	FRDGBufferRef VertexBuffer;   // empty => vertex positions generated in-shader
	std::uint32_t VertexOffset = 0;
	std::uint32_t VertexCount = 3;
	std::uint32_t InstanceCount = 1;
	FRDGBufferRef IndexBuffer;    // empty => non-indexed draw
	std::uint32_t IndexOffset = 0;
	std::uint32_t IndexCount = 0;
	bool bIndex32 = true;
};

/**
 * Draw list (protocol): the mesh batches of ONE subpass. One AddPass corresponds
 * to one subpass, so a single DrawList is that subpass's draw set -- no subpass
 * grouping key. AddPass consumes it and records the draws; producers (FScene's
 * hardcoded triangle today, later a scene renderer) fill it.
 */
class MAHO_RENDER_API FDrawList
{
public:
	void Add(const FDrawBatch& Batch) { Batches.push_back(Batch); }

	[[nodiscard]] const std::vector<FDrawBatch>& GetBatches() const { return Batches; }

private:
	std::vector<FDrawBatch> Batches;
};

} // namespace Maho
