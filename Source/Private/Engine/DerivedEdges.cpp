#include <Engine/DerivedEdges.h>

#include <Engine/StageMetadata.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

namespace
{
	/** One access, with the node that declared it. */
	struct FAccessRef
	{
		std::size_t                Node = 0;
		const FResourceAccess*     Access = nullptr;
	};

	/** How old a phase is, in rounds: 0 = the round being built, K-1 = the oldest the graph still
	 *  holds. (Kept for the cross-round rules a later step adds; this round derives same-round pairs
	 *  only, see below.) */
	[[maybe_unused]] [[nodiscard]] std::int32_t AgeOf(std::int32_t Phase, std::int32_t CurrentPhase,
		std::int32_t K)
	{
		const std::int32_t Age = CurrentPhase - Phase;
		return Age >= 0 ? Age : Age + K;
	}

	/** The ordering an access PAIR implies, or `nullptr` when the semantics say nothing about it.
	 *
	 *  Producer A precedes consumer B when:
	 *    * A CREATES what B uses            -- `Create < any access`        (Lifetime)
	 *    * A uses what B DESTROYS           -- `any access < Destroy`       (Lifetime)
	 *    * both WRITE the same thing        -- `Write < Write`        (Strict only)
	 *    * A writes what B reads            -- `Write < Read`         (Strict only; NOT for Snapshot,
	 *                                          whose whole point is that the two sides are scheduled
	 *                                          apart and nothing can tear)
	 *  `Destroy` before `Create` is the next incarnation, not an ordering, so it implies nothing. */
	[[nodiscard]] EDependencyType OrderingOf(const FResourceAccess& A, const FResourceAccess& B,
		EDependencyType Out)
	{
		const bool bStrict = A.Ordering == EResourceOrdering::Strict;

		if (A.Access == EResourceAccess::Create && B.Access != EResourceAccess::Create
			&& B.Access != EResourceAccess::Destroy)
		{
			return EDependencyType::Lifetime;
		}
		if (B.Access == EResourceAccess::Destroy && A.Access != EResourceAccess::Destroy
			&& A.Access != EResourceAccess::Create)
		{
			return EDependencyType::Lifetime;
		}
		if (bStrict && A.Access == EResourceAccess::Write && B.Access == EResourceAccess::Write)
		{
			return EDependencyType::Shared;
		}
		if (bStrict && A.Access == EResourceAccess::Write && B.Access == EResourceAccess::Read)
		{
			return EDependencyType::Shared;
		}
		return Out;                                  // "no edge" (Out is the caller's `Wait` sentinel)
	}

	[[nodiscard]] std::string ReasonOf(const FResourceAccess& A, const FResourceAccess& B)
	{
		std::string Reason = "resource '";
		Reason.append(A.Resource);
		Reason.append("': ");
		Reason.append(ToString(A.Access));
		Reason.append(" -> ");
		Reason.append(ToString(B.Access));
		if (A.Ordering != EResourceOrdering::LifetimeOnly)
		{
			Reason.append(" (");
			Reason.append(ToString(A.Ordering));
			Reason.append(")");
		}
		return Reason;
	}

	/** What a node's stage declares, or nullptr when it declares nothing (or has no row at all). A
	 *  frame that implements a stage through the INTERFACE declaration only (`Frame == Stage`) is
	 *  found by the fallback lookup, which is why a row is asked for twice. */
	[[nodiscard]] const FStageAccessRow* RowOf(const FTaskKey& Key)
	{
		if (const FStageAccessRow* Row = FindStageAccess(Key.Name, Key.Stage))
		{
			return Row;
		}
		return FindStageAccess(Key.Stage, Key.Stage);
	}
}

void FResourceEdgeDeriver::DeriveEdges(std::span<const FTaskKey> LiveKeys, std::uint64_t Frame,
	std::vector<FDerivedEdge>& Out)
{
	const std::int32_t K = GetFramesInFlight();
	if (K <= 0 || LiveKeys.empty())
	{
		return;
	}

	// Aggregate one round's accesses BY RESOURCE: the whole point of the derivation is that a writer
	// and a reader of the same thing find each other without either naming the other.
	std::unordered_map<std::string_view, std::vector<FAccessRef>> ByResource;
	for (std::size_t Index = 0; Index < LiveKeys.size(); ++Index)
	{
		const FStageAccessRow* Row = RowOf(LiveKeys[Index]);
		if (Row == nullptr)
		{
			continue;
		}
		for (std::size_t A = 0; A < Row->AccessCount; ++A)
		{
			const FResourceAccess& Access = Row->Accesses[A];
			if (Access.Resource.empty())
			{
				continue;
			}
			ByResource[Access.Resource].push_back(FAccessRef{ Index, &Access });
		}
	}

	for (const auto& [Resource, Entries] : ByResource)
	{
		for (const FAccessRef& Producer : Entries)
		{
			for (const FAccessRef& Consumer : Entries)
			{
				if (Producer.Node == Consumer.Node)
				{
					continue;                        // a stage is not ordered against itself
				}
				const FTaskKey& From = LiveKeys[Producer.Node];
				const FTaskKey& To = LiveKeys[Consumer.Node];
				if (From.Phase != To.Phase || From.Name == To.Name)
				{
					// THIS ROUND only, and only ACROSS frames.
					//
					// Same round is the whole scope because that is where nothing else orders the pair:
					// inside one frame the stage chain already does, and a cross-FRAME pair in one round
					// is exactly the fact the accesses state and no declaration carries.
					//
					// Cross-ROUND pairs are deliberately NOT derived. An edge is applied when a batch
					// merges, so pointing at a node from an older round can name one that will never run
					// again (an init-phase stage runs once) and that has already been reaped -- the
					// consumer would then wait forever. Per-round recurrence is already covered by each
					// stage's DECLARED self edge (its next round waits for its own previous one), which is
					// the piece that makes this scope sufficient rather than merely safe.
					continue;
				}

				const EDependencyType Type = OrderingOf(*Producer.Access, *Consumer.Access,
					EDependencyType::Wait);
				if (Type == EDependencyType::Wait)
				{
					continue;                        // these two modes imply nothing
				}
				Out.push_back(FDerivedEdge{ To, From, Type, ReasonOf(*Producer.Access, *Consumer.Access) });
			}
		}
	}
}

FResourceEdgeDeriver& GetResourceEdgeDeriver()
{
	static FResourceEdgeDeriver Deriver;
	return Deriver;
}

}
