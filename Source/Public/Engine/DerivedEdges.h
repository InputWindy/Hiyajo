/// @file
/// @brief Deriving a frame graph's edges from RESOURCE ACCESS (`Engine/StageMetadata.h`), not from
///        hand-written declarations.
///
/// A stage declares what it touches (`MAHO_READ` / `MAHO_WRITE` / `MAHO_CREATE` / `MAHO_DESTROY`), and
/// the resource it touches carries its own ordering semantics (`MAHO_DECLARE_RESOURCE_ORDERING`). This
/// turns those two facts into the edges that were previously written by hand, one pair at a time, and
/// discovered by crashing.
#pragma once

#include <Core/FrameGraph.h>

namespace Maho
{

/// @brief The engine's edge deriver: aggregates one round's accesses BY RESOURCE and emits the pairs
///        the resource's ordering semantics imply (see design D4/D5).
///
/// @note It emits only OLDER-round -> this-round pairs, and never a same-round pair: inside one round
///       every stage pair is already ordered by the frame's own stage chain, so a same-round edge
///       would be redundant at best and a cycle at worst. `Snapshot` deliberately yields no Write ->
///       Read edge at all (nothing can tear, and the edge would serialise the K-deep pipeline).
class MAHO_API FResourceEdgeDeriver final : public IDerivedEdgeSource
{
public:
	void DeriveEdges(std::span<const FTaskKey> LiveKeys, std::uint64_t Frame,
		std::vector<FDerivedEdge>& Out) override;
};

/// @brief The process-wide deriver the hosts install (`FFrameGraph::SetDeriver`).
MAHO_API FResourceEdgeDeriver& GetResourceEdgeDeriver();

}
