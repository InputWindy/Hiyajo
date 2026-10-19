/// @file
/// @brief The node vocabulary and the BUILDER protocol: what a node IS, how a layer describes a batch of
///        them, and what a layer guarantees about the keys it computes.
///
/// This header is the seam between the backend (`Core/Task/TaskGraph.h`, which RUNS nodes) and the layers above
/// (which DECLARE them). It is the lower half of that pair -- `TaskGraph.h` includes this one -- so the
/// graph knows "there is a builder protocol, ask it for a batch" while knowing nothing about HOW a batch
/// comes to exist. Everything a layer decides lives on this side of the line, above all the rule the graph
/// must stay ignorant of: **deriving dependency edges from the RESOURCES a node declares it reads and
/// writes**. That rule is a layer's business (see `Engine/DerivedEdges.{h,cpp}` and the change
/// `add-resource-metadata-edges`); what crosses into Core is a key, a closure, and a list of predecessor
/// keys -- never a resource.
///
/// ## A key is the layer's vocabulary (the contract)
///
/// A key is a 64-bit VALUE the caller computes -- usually a hash, but the graph has no opinion on how it
/// was produced: it receives the RESULT and nothing else. A key is not required to come from a string (a
/// caller can key by whatever it already has: a slot, an index, a round), and naming a node therefore
/// costs no allocation and no storage, with nothing passed in having to outlive anything.
///
/// What the layer must guarantee:
///
///   * **Distinct for the graph's WHOLE LIFE** -- every submission must carry a key no earlier submission
///     used. The graph can only CHECK the live half of that (a key still in flight is refused and
///     reported); a collision with an already-COMPLETED node is invisible to it, because completing is
///     exactly what erases the key -- and that half's symptom is silent: a lookup that should have MISSED
///     (its node is done) would bind to the newer node wearing the same name. The half the graph cannot see
///     is therefore the caller's to guarantee.
///   * **Recomputable** -- the caller can produce the same value again from what it already knows. That is
///     what makes "nothing comes back" sufficient: there is no token to hold.
///   * **Carries WHICH time, not WHICH slot** -- a caller running the same work repeatedly must fold the
///     repetition's identity in (a ROUND NUMBER), never an index modulo some depth: `Round % K` repeats by
///     construction (`Round` and `Round + K` are the same key), and a repeat fails twice over -- the new
///     submission collides with an older node still in flight (refused), and any lookup that should have
///     MISSED instead binds to the newer node wearing the same name (an edge to the wrong node, i.e. the
///     "parent that already finished" reappearing). Absolute, never folded.
///
/// `Key` starts as `INDEX_NONE` -- the value a caller that never computed a key carries -- and a submission
/// with it is refused rather than silently shared with every other caller that forgot.
///
/// ## A name that no longer binds is not an error
///
/// A node leaves the graph's table by COMPLETING, so a key that cannot be resolved names a constraint that
/// is already satisfied: a `Predecessors` entry that does not resolve is SKIPPED (an edge to it would
/// order nothing), and a `Wait` on it returns at once. That one rule is also how a throttling edge works
/// out -- "the first node of round N waits for the last node of round N-K" is enforced exactly while that
/// node is in flight, and is a no-op once it has completed. THIS is why a key has to carry the absolute
/// round: with a folded key, the "no-op" case would sometimes be a BINDING instead.
///
/// ## The resource side is the LAYER's (and why this header names none of it)
///
/// The layer's rule -- "derive a node's predecessors from the resources it declares it reads and writes" --
/// is stated and implemented ENTIRELY above Core: `Engine/ResourceName.h` (`MAHO_DECLARE_RESOURCE` and the
/// name trait a resource is identified by), `Engine/StageMetadata.h` (`EResourceAccess`, the ordering
/// trait and `MAHO_DECLARE_RESOURCE_ORDERING`, the `ICreate/IDestroy/IRead/IWrite` capability markers, and
/// the `(frame, stage) -> accesses` table a declaration registers into), `Engine/StageAccess.h` plus the
/// `MAHO_ACCESS_*` / `MAHO_READ`-style vocabulary a declaration site is written with, and
/// `Engine/DerivedEdges.*` -- the rules that turn accesses into edges.
///
/// This header deliberately names NONE of that, for two independent reasons:
///
///   1. **Layering**: Core may not depend on Engine. The other direction would put the engine's semantics
///      underneath its own scaffolding, which is what this pair of headers exists to prevent.
///   2. **It is not this protocol's business**: a builder is handed the layer's own instance and returns
///      keys and closures. HOW a layer decides which keys to name is the layer's (see `TTaskBuilder`).
///
/// So a layer's builder consults those headers itself, inside its own `Build`, and hands back a `FTaskBatch`.
#pragma once


#include <concepts>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <utility>

namespace Maho
{
namespace Core
{

/// @brief A node's identity, as the graph uses it: a 64-bit value the CALLER computes.
///
/// How it is computed is the caller's business -- a name's hash, a slot, an index, or any mix of them.
/// The graph only ever receives the result, compares it, and hands it back in its diagnostics.
using FTaskKey = std::uint64_t;

/// @brief The value `FTaskDesc::Key` starts as: what a caller that never computed a key carries.
///
/// It is all bits set (`-1` read as unsigned) -- a value no real key should ever be, and one the graph
/// REFUSES rather than silently sharing it with every other caller that forgot.
inline constexpr FTaskKey INDEX_NONE = ~static_cast<FTaskKey>(0);

/// @brief One execution node, as the CALLER describes it: the key it is named by, and what to run.
struct FTaskDesc
{
	/// @brief The node's identity (see the key contract in the file comment). It starts at `INDEX_NONE`,
	///        the "no key" value -- a submission carrying it is refused, not quietly shared.
	FTaskKey Key = INDEX_NONE;

#if MAHO_DO_CHECK
	/// @brief The node's NAME, for a hang's dump and for a refusal's message. A DIAGNOSTIC, so it is built
	///        into the graph only where diagnostics are (Debug/Release) and dropped where they are not
	///        (Shipping); it is never what the node is addressed by. Must be NUL-terminated (a literal, or
	///        a `c_str`).
	///
	///        The field therefore only EXISTS in some configurations. Filling a description is the caller's
	///        business, so a caller that wants that difference hidden writes its own helper -- this header
	///        deliberately has none: how a caller builds its nodes is not the graph's vocabulary.
	std::string_view DebugKey;
#endif

	/// @brief What to run, on the pool's work lane.
	std::function<void()> Body;
};

/// @brief What a BUILDER hands back: the batch to submit, as VIEWS into storage the builder owns.
///
/// The views are normally into the builder's own reusable buffers (the instance's arrays, typically), so
/// producing a batch costs no allocation at all once those buffers exist. The graph consumes the batch
/// immediately -- it copies the keys into its own lane messages and takes each description BY VALUE -- so
/// the storage behind these views only has to live until the submitting call returns.
struct FTaskBatch
{
	/// @brief The nodes, in submission order (each with its OWN key).
	std::span<const FTaskDesc> Descs{};

	/// @brief Keys EVERY node of the batch waits for.
	std::span<const FTaskKey> Predecessors{};

	/// @brief Keys to wait for BEFORE submitting any of them.
	std::span<const FTaskKey> Completed{};
};

// ── the resource side is the LAYER's (see the file comment) ───────────────────────────────────────────
//
// This header deliberately names no resource, no access mode and no ordering category: that vocabulary
// already exists above Core (`Engine/ResourceName.h`, `Engine/StageMetadata.h`, `Engine/StageAccess.h`),
// it is semantic, and Core may not depend on the layer. A builder consults those headers inside its own
// `Build` and hands back a `FTaskBatch`.

/// @brief The extension point: "how MY instance becomes a batch of nodes".
///
/// It is a customization point rather than a virtual interface, because the graph calls it SYNCHRONOUSLY
/// on the submitting thread, and only when that form of `Submit` is used: a layer that never uses it pays
/// nothing, and a layer that does writes one specialization next to its own type -- the graph stays
/// ignorant of what the instance was, exactly as it is ignorant of frames.
///
/// **This is where a layer's node rules belong**: which nodes a round has, what each one waits for, and --
/// for a layer that models resources -- which resources each node reads and writes, from which IT derives
/// the predecessor keys. None of that crosses into the graph.
///
/// The primary template has NO `Build`: a type without a specialization is then not buildable at all --
/// which `CTaskBuildable` below turns into "the extension `Submit` simply does not match", instead of a
/// link error (a declared-but-undefined template used to make the compiler instantiate the incomplete
/// type; an empty, COMPLETE primary keeps the check a plain member lookup).
///
/// Where the specialization goes matters: a namespace-scope specialization belongs to the namespace the
/// TEMPLATE lives in, not the one the type lives in. Its KEY is the type AND the argument types -- the
/// pair `Submit` looks up, which is why the extension decays them (a caller passing `Round&` and one
/// passing `Round` must land on the same builder):
///
///     template <>
///     struct TTaskBuilder<Maho::Engine::FFrame, std::uint64_t>
///     {
///         static bool Build(FTaskBatch& Out, FFrame& Frame, std::uint64_t Round);
///     };
template <typename T, typename... TArgs>
struct TTaskBuilder
{
};

/// @brief Whether `T` has a builder for exactly these arguments -- and one whose `Build` answers `bool`.
///
/// This is the overload GUARD: without it the extension `Submit` would be a candidate for every call (its
/// `T` can bind to anything), so a type without a builder would fail on a missing `Build` rather than
/// simply not matching.
template <typename T, typename... TArgs>
concept CTaskBuildable = requires (FTaskBatch& Batch, T& Instance, TArgs&&... Args)
{
	{ TTaskBuilder<T, TArgs...>::Build(Batch, Instance, std::forward<TArgs>(Args)...) } -> std::same_as<bool>;
};

}
}
