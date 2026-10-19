/// @file
/// @brief The execution BACKEND: keyed work items, the order between them, and running them in parallel
///        on a thread pool.
///
/// This is the layer under the frame system, and it is deliberately ignorant of it: a node here is "a
/// closure, named by a key", and an edge is "this had to happen before that". It knows nothing about
/// frames, stages, rounds, phases, windows or resources -- a frame is one ORGANISATION of nodes, and it
/// lives in the engine layer on top (see `Engine/FrameBuilder.h`, `Engine/Frame.h`).
///
/// ## Shape: the caller hands over a key, the graph owns everything else
///
/// `Submit` takes `(Key, Body)` and RETURNS -- nothing comes back and nothing has to be kept. Order is
/// declared by NAMING other keys ("this runs after that"), which is what makes the graph a DAG that
/// grows while it RUNS: there is no round, no barrier and no second "start" step, `Submit` IS the start.
/// Everything else -- a node's storage, its place in the heap, its in-flight/completed state -- is the
/// graph's private business, reached by key through a table only the scheduler touches.
///
/// `FTaskGraph` is a resident role (`FThreadedServer`, `Core/Task/ThreadPool.h`): it owns a persistent lane
/// whose thread is the SCHEDULER, and it runs the bodies on a WORK lane of the same pool. THE POOL DOES NOT
/// SORT WORK BY GRAPH: every graph in the process dispatches its bodies into the SAME work lane (the one
/// it was constructed with -- `DefaultLane` by default), so all of their nodes sit in one queue and
/// whichever worker is free takes the next one; workers are never reserved for, or kept away from, a
/// graph. What a graph owns privately is its SCHEDULER (its own lane and thread, its table, its fence):
/// that coordinates it, it does not isolate it. Two consequences the layer designs around: one graph's
/// flood WILL consume the pool (there is no fairness or priority to appeal to -- shape the submission
/// instead), and `Wait()` fences only the graph it was called on ("everything quiet" means waiting on each
/// of them). That is what
/// keeps the graph lock-free -- and it is why the table is private, and why `Wait(keys)` is the ONE
/// request whose answer the caller needs:
///
///   * `Submit` posts the node and returns (fire and forget);
///   * the scheduler pushes it into the heap and pops + dispatches everything whose unmet count is 0
///     (a node's place in the heap IS its "not yet dispatched" state -- popped means running);
///   * a worker that finishes a body posts ONE notification back; the scheduler forgets the key, recycles
///     the storage, decrements its successors' counts and dispatches whatever became ready.
///
/// The heap is indexed (a node caches its own slot in it) and equal counts are broken by submission
/// order, so the dispatch order of one graph does not drift between runs.
///
/// ## A key is the layer's vocabulary (the contract)
///
/// See `Core/Task/TaskBuilder.h`: the key, the description and the batch -- and the four things a layer must
/// guarantee about the keys it computes -- are defined there, together with the builder protocol that
/// turns a layer's own type into a batch of nodes. This header takes them as given and adds the graph that
/// RUNS them. The one clause worth repeating here, because it is what makes the graph's lookups mean
/// anything:
///
///   * **Distinct for the graph's WHOLE LIFE** -- the graph can only check the LIVE half of that (a key
///     still in flight is refused); the half it cannot see (a collision with a COMPLETED node) is the
///     caller's to guarantee, and its symptom is silent.
///
/// ## A name that no longer binds is not an error
///
/// A node leaves the table by COMPLETING, so a key that cannot be resolved names a constraint that is
/// already satisfied: a `Predecessors` entry that does not resolve is SKIPPED (an edge to it would order
/// nothing), and a `Wait` on it returns at once. That one rule is also how a throttling edge works out --
/// "the first node of round N waits for the last node of round N-K" is enforced exactly while that node is
/// in flight, and is a no-op once it has completed. THIS is why a key has to carry the absolute round (see
/// the key contract in `TaskBuilder.h`): with a folded key, the "no-op" case would sometimes be a BINDING
/// instead -- a newer node would be wearing that name, and the edge would attach to it.
///
/// ## The instruction set, and what it deliberately does NOT do
///
/// Three verbs plus the builder door: `Submit` (one node, a batch, or whatever an upper layer's
/// `TTaskBuilder` makes of an instance), `Wait` (the fence, with or without a key list). That is the whole
/// service, and it is UNRELIABLE ON PURPOSE: the graph keeps its OWN state consistent and reports what it
/// notices (a refusal, a stall dump), but it is not a safety net. Every guarantee about what the work
/// MEANS -- that keys never repeat over the graph's life, that a key being referenced really was submitted,
/// that a round is admitted at the right moment -- belongs to a design the LAYER owns; the key contract
/// above is the place where that design meets this one.
///
/// ## The hardening list: what this service does NOT protect you from
///
/// "Unreliable on purpose" (above) has concrete clauses. Each one is a case the graph will NOT cover for
/// you; the symptom and the layer's hardening are given together, because this header is the only contract
/// surface the layer reads.
///
/// 1. **A submission racing `Shutdown`.** A post that lands after the role stopped is DROPPED and reported
///    (`FThreadedServer::Submit: the role is not running ... the task was dropped`): that work does not
///    happen, while the rest of the graph stays consistent (it never came in). CHECKED, as far as it can
///    be: `PostToLane` blocks on the spot when the graph is not running at all (the deterministic half --
///    after `Shutdown` or before `Initialize`), which is where a violated lifetime shows up -- as far as
///    that check exists, the diagnostic builds being the ones that carry it. Hardening for the racing half: whoever may still submit stops before teardown and confirms the fence with one
///    `Wait()` first.
/// 2. **Destroying the graph while another thread is inside `Submit`/`Wait`.** UNDEFINED (the fence's mutex
///    and CV are destroyed under the waiter), so this one is NOT left to the layer: `Wait` counts its
///    callers and the destructor BLOCKS if any of them is still inside -- which costs nothing where the
///    hot path is, since `Wait` blocks anyway. A concurrent `Submit` during destruction stays the layer's
///    business (its window is a single post, not a blocking call).
/// 3. **A node's `Unblocks` capacity is per-cell high-water and never shrinks.** A fan-out spike is kept by
///    that cell for good (8 bytes per entry times the peak). Not a leak, but the peak never comes back.
///    NOT CHECKED, because "unreasonable fan-out" is a property of the layer's design, not a number this
///    graph could pick: hardening is the layer keeping one node's fan-out sane (e.g. do not hook a whole
///    round's nodes onto a single convergence node).
/// 4. **Wraparound.** The ordinals that decide identity and order (`Generation`, `Sequence`) are 64-bit, so
///    their wrap is unreachable. The two 32-bit ones left are bounded by what the graph can hold at once
///    (`HeapPos`: 2^32-1 dispatchable nodes; `Unmet`: 2^32-1 in-edges on one node), both far past what a
///    process can allocate. NOT CHECKED, and nothing to harden.
///
/// ## Why the extra namespace
///
/// Everything above lives in `Maho::Core`, not in `Maho` directly: this backend's vocabulary
/// (`FTaskKey`, `FTaskDesc`) is a different thing from the frame layer's `FTaskKey` (Core/FrameGraph.h's
/// `{Name, Stage, Phase}` triple) -- a frame COMPUTES one of these from one of those. Scoping keeps both
/// names honest instead of renaming either one. (The rest of Core still lives in plain `Maho`: this is
/// the backend's own boundary, not a claim that every Core header has moved.)
#pragma once


#include <Core/Export.h>
#include <Core/Fatal.h>
#include <Core/Task/TaskBuilder.h>          // the node vocabulary + the builder protocol: this header RUNS them
#include <Core/Task/ThreadPool.h>

#include <functional>
#include <memory>
#include <span>
#include <utility>

namespace Maho
{
namespace Core
{

/// @brief A directed acyclic graph of keyed execution nodes, run in parallel on a pool.
class MAHO_API FTaskGraph final : public FThreadedServer
{
public:
	/// @brief Creates the graph. Call `Initialize()` (inherited) before submitting anything.
	/// @param InPool The pool to reside on (it must outlive the graph).
	/// @param WorkLane The lane the node BODIES run on -- usually the owner's shared lane. The scheduler
	///        itself runs on this role's own persistent lane (see `FThreadedServer`).
	FTaskGraph(FThreadPool& InPool, FThreadPool::FLane WorkLane = FThreadPool::DefaultLane);

	/// @brief Drains and shuts the graph down (queued work completes first: a body may be running).
	~FTaskGraph() override;

	FTaskGraph(const FTaskGraph&) = delete;
	FTaskGraph& operator=(const FTaskGraph&) = delete;

	/// @brief Hands ONE node over -- the ATOMIC, NON-BLOCKING operation, and the only way into the graph.
	///
	/// It posts the node and RETURNS: nothing comes back, and there is nothing to wait for here. The keys
	/// it names as predecessors must already have been submitted (a new node can only be the "later" end
	/// of an edge), which is what makes the DAG acyclic by construction.
	/// @param Desc The closure to run, and the key it is named by.
	/// @param Predecessors Keys that must COMPLETE before this node RUNS (empty = as soon as possible).
	///        A key that does not resolve is skipped, silently -- see the file comment.
	void Submit(FTaskDesc Desc, std::span<const FTaskKey> Predecessors = {});

	/// @brief Hands over a BATCH of nodes that share their predecessors -- sugar over `Wait` + `Submit`.
	///
	/// It first waits for `Completed` (see `Wait`), then submits every node with the same predecessor
	/// list, in order. Use the single-node form when the nodes' edges differ or when nothing may block.
	/// @param Descs The nodes, in submission order (each with its OWN key -- a batch shares predecessors,
	///        not identity).
	/// @param Predecessors Keys EVERY node of the batch waits for.
	/// @param Completed Keys to wait for BEFORE submitting any of them.
	void Submit(std::span<const FTaskDesc> Descs, std::span<const FTaskKey> Predecessors = {},
		std::span<const FTaskKey> Completed = {});

	/// @brief Submits whatever the upper layer's BUILDER makes of `Instance` -- the extension form.
	///
	/// It asks `TTaskBuilder<T, ...>::Build` for a batch and hands it to the batch `Submit` above, so the
	/// semantics are exactly those: fire and forget, with `Completed` waited for first. The graph never
	/// learns what `Instance` was.
	/// @tparam T The layer's own type (it must have a `TTaskBuilder` -- see `CTaskBuildable`).
	/// @param Instance The object whose builder turns it into nodes. By NON-const reference on purpose: a
	///        builder typically writes into the instance's own reusable buffers.
	/// @param InArgs Whatever the builder needs (a round number, a context, ...).
	/// @return False when the builder had nothing to submit -- a normal answer, not an error, and the only
	///         thing this form reports (the work itself is never waited for here).
	template <typename T, typename... TArgs>
		requires CTaskBuildable<T, std::decay_t<TArgs>...>
	bool Submit(T& Instance, TArgs&&... InArgs)
	{
		FTaskBatch Batch;
		if (!TTaskBuilder<T, std::decay_t<TArgs>...>::Build(Batch, Instance, std::forward<TArgs>(InArgs)...))
		{
			return false;
		}
		Submit(Batch.Descs, Batch.Predecessors, Batch.Completed);
		return true;
	}

	/// @brief Blocks until every submission so far has completed -- the submission FENCE -- and, when keys
	///        are given, until THOSE nodes have completed as well.
	///
	/// It is a fence, not a drain: the caller may submit more afterwards. The key list is what a caller
	/// that cannot hand work over yet needs (it is about to reuse something those nodes still own, or it
	/// must not queue work that cannot run) -- `Submit` itself never blocks. A key that does not resolve
	/// has already completed, so it returns at once.
	///
	/// Blocking a pool WORKER on another node can starve the pool (that node needs a worker too), so this
	/// belongs on a driver/owner thread, never inside a node body.
	/// @param Keys Nodes that must have completed for this call to return (empty = just the fence).
	void Wait(std::span<const FTaskKey> Keys = {});

	/// There is deliberately no "which node is running now" accessor: nodes run in PARALLEL, so "now" is
	/// a property of a thread, not of the graph -- a trace that wants it asks the trace, per thread.

protected:
	/// @brief The role's lane name (see `FThreadedServer`): what a trace calls the scheduler's row.
	/// @return A literal.
	[[nodiscard]] const char* GetThreadName() const override;

private:
	/** Waits (on the CALLING thread) until every given key has completed -- the key list of `Wait` (and
	 *  therefore of the batch `Submit`). A key that does not resolve has already completed and is not
	 *  waited on; the resolution itself has to ask the scheduler, because the table is its private
	 *  state. */
	void AwaitCompletions(std::span<const FTaskKey> Keys);

	/** Post to the scheduler's lane. The lane is the role's internals (`FThreadedServer::Submit` is
	 *  protected), and the graph's implementation class -- where the heap and the table live -- reaches
	 *  it through here (a nested class may use the enclosing class's privates, not its base's).
	 *
	 *  A graph that is NOT running would have this message DROPPED by the base (reported, then gone), and
	 *  the layer's contract says the graph outlives whoever submits to it -- so reaching here means a
	 *  lifetime was violated, and that is blocked rather than silently lost. (A true race with `Shutdown`
	 *  still lands in the base's report: the post returns nothing, so the graph cannot learn it was
	 *  refused. See the hardening list in the file comment.) */
	void PostToLane(std::function<void()> Task)
	{
		MAHO_CHECKF(IsRunning(), "FTaskGraph: a message was posted at a graph that is not running (after "
			"Shutdown, or before Initialize)");
		FThreadedServer::Submit(std::move(Task));
	}

	/// @brief Drains the lane: every queued bookkeeping message is applied while this object is alive.
	///        (`FThreadedServer::Shutdown` alone would leave messages in flight past the table's life.)
	void Shutdown() override;

	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

}
}
