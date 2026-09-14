#pragma once

#include <Core/ThreadPool.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

/**
 * Frames in flight = the submit-ring depth. The graph keeps this many independent
 * per-frame state slots, so frame N+1 may be in flight while frame N is still
 * executing (Vulkan-style: one frame recording + K-1 executing). Raise it to hide
 * more of the frame tail; lower it to clamp per-frame memory. Overridable per
 * target with a compile definition.
 */
#ifndef MAHO_FRAMES_IN_FLIGHT
#	define MAHO_FRAMES_IN_FLIGHT 3
#endif

/**
 * Debug-only stall audit: a frame still in flight this many ms after its submit is
 * reported (once) with the nodes and gates that are holding it. The graph's failure
 * mode is "a missing edge = a hang", so it has to say so out loud instead of just
 * never returning.
 */
#ifndef MAHO_TASKGRAPH_STALL_MS
#	define MAHO_TASKGRAPH_STALL_MS 5000
#endif

namespace Maho
{

/** Fence of one submitted frame (from SubmitFrame; 0 = nothing was submitted). */
using FFrameFence = std::uint64_t;

/** Named dep of a node: { dep object name, dep's stage }. */
struct FTaskGraphDependency
{
	std::string     Name;
	std::type_index Stage = std::type_index(typeid(void));   // void = unset
};

/** Base task-graph node -- pure topology. Subclasses extend it with the execution payload. */
struct FTaskGraphNode
{
	std::string                         Name;          // object identity (e.g. "World")
	std::type_index                     Stage = std::type_index(typeid(void));  // void = unset
	std::vector<FTaskGraphDependency>  Dependencies;   // edges: (name, stage) -> this node
};

/**
 * Dependency-graph scheduler. Schedules NODES, where a node is an (object,
 * stage) pair. Edges come from each node's dependency tuples. A node is ready
 * when ALL of its direct dependencies have completed -- the graph is
 * stage-agnostic, so a node whose deps finished is released immediately (no
 * stage barrier -- this enables cross-stage pipelining).
 *
 * Lifecycle:
 *   Init        -- load the full node set (topology data only). Requires an idle
 *                  graph: the rebuild replaces the node storage.
 *   Compile     -- wire edges + detect cycles/missing deps -> bool. On failure the
 *                  offending node name is available via GetCompileErrorNode().
 *                  Sizes the per-frame ring (MAHO_FRAMES_IN_FLIGHT slots).
 *   SubmitFrame -- submit ONE frame into the ring; returns its fence. Up to
 *                  MAHO_FRAMES_IN_FLIGHT frames may be in flight at once: the call
 *                  blocks only for the ring slot it is about to reuse (the old
 *                  per-frame Flush barrier is gone).
 *   WaitFence   -- block until one submitted frame finished.
 *   WaitAll     -- block until every submitted frame finished (Flush is an alias;
 *                  the install/unload graphs drain through it).
 *
 * Cross-frame scheduling: each submit gets its own ring slot (MAHO_FRAMES_IN_FLIGHT
 * slots), so per-frame state no longer has to be Reset between frames, and
 * SubmitFrame keeps its own slot-reuse guard (it waits for the frame it overwrites).
 *
 * Frames DO overlap. Cross-frame safety comes from the per-layer gate (FGroupGate):
 * a layer's root node either CLAIMS its group or PARKS until the running instance's
 * sink hands the gate over, so frame N+1 of a layer never overlaps its own frame N
 * (its stage methods touch the layer's frame state, and its async resources assume
 * the previous frame is done) -- while DIFFERENT layers still pipeline across frames.
 * The hand-over is count-free (claim-or-park + exactly one sink pop per instance),
 * so there is no counter to drift out of sync.
 *
 * Execution protocol is delegated to subclasses via ExecuteNode(): the base
 * FTaskGraphNode carries only {Name, Stage, Dependencies}; a subclass defines
 * its own node (holding whatever callback/context it needs) and casts it back
 * inside ExecuteNode.
 *
 * Failure isolation: a throwing ExecuteNode is caught and reported (ReportError,
 * non-fatal) and its downstreams are still released -- a buggy layer never kills
 * the host nor hangs the graph.
 */
class FTaskGraph
{
public:
	using FDependency = FTaskGraphDependency;
	using FNode = FTaskGraphNode;

	explicit FTaskGraph(FThreadPool& InPool)
		: Pool(InPool)
	{
	}

	FTaskGraph(const FTaskGraph&) = delete;
	FTaskGraph& operator=(const FTaskGraph&) = delete;
	virtual ~FTaskGraph();   // waits for in-flight frames (tasks reference this)

	/** 1. Load the full node set (topology data only). Waits for in-flight frames
	 *  first (the rebuild replaces the node storage) and reports when it had to. */
	void Init(std::vector<FNode*> Nodes);

	/** 2. Wire edges + validate (cycle / missing dep). Returns false on error;
	 *  the offending node (== the layer name) is in GetCompileErrorNode(). */
	bool Compile();

	/** 3. Submit one frame (== SubmitFrame; kept for the install/unload graphs). */
	void Execute();

	/** Submit ONE frame, returning its fence. Blocks only for the ring slot it is
	 *  about to reuse (frame - MAHO_FRAMES_IN_FLIGHT), never for the whole previous
	 *  frame -- that is what lets frame N+1 overlap frame N. */
	[[nodiscard]] FFrameFence SubmitFrame();

	/** Block until the given frame finished. Fence 0 (nothing submitted) returns. */
	void WaitFence(FFrameFence Fence);

	/** Block until every submitted frame finished (the quiescence point). */
	void WaitAll();

	/** Alias of WaitAll -- the drain step of the install/unload graphs. */
	void Flush();

	/** True when no frame is in flight. */
	[[nodiscard]] bool IsIdle() const noexcept;

	/** Name of the node whose dependency broke the last Compile (empty if none).
	 *  Since node Name == the layer name, this identifies the offending layer. */
	[[nodiscard]] const std::string& GetCompileErrorNode() const { return CompileErrorNode; }

protected:
	/**
	 * Execution protocol hook -- the base graph only knows a node is ready; the
	 * subclass casts FNode to its own derived node and drives the callback.
	 * Runs on a pool worker thread (must be thread-safe).
	 */
	virtual void ExecuteNode(FNode* Node) = 0;

	FThreadPool& Pool;

private:
	struct FTask
	{
		FNode* Node = nullptr;
		std::vector<std::size_t> Downstreams;   // task indices this one releases
		std::uint32_t InitPending = 0;          // compiled initial dep count
		std::uint32_t Group = 0;                // group index (== node Name == layer)
		bool          bRoot = false;            // group's first node: claims the cross-frame gate
		bool          bSink = false;            // group's last node: hands the gate over
	};

	/** Per-layer cross-frame gate -- one instance of a layer at a time.
	 *
	 *  A layer's frame N+1 must not overlap its frame N: its stage methods touch the
	 *  layer's frame state, and its async resources assume the previous frame is done
	 *  (the RHI owns a single in-flight fence). DIFFERENT layers still pipeline.
	 *
	 *  The hand-over is deliberately count-free: a group's root either CLAIMS the
	 *  group or PARKS (and is then not dispatched at all), and the instance's single
	 *  sink pops exactly one parked root. One instance = one report = one pop, so
	 *  there is no counter to drift out of sync (a drifting count wedged a layer
	 *  permanently in the first version of this mechanism). */
	struct FGroupGate
	{
		std::mutex                                             M;
		std::deque<std::pair<std::uint64_t, std::size_t>>       Waiting;   // parked roots (frame, task)
		std::uint64_t                                          RunningFrame = 0;
		bool                                                   bBusy = false;
	};

	/** One ring slot == one frame's independent state. Reused every K frames, so
	 *  SubmitFrame waits for that frame's fence before touching it. */
	struct FFrameSlot
	{
		std::unique_ptr<std::atomic<std::uint32_t>[]> Pending;              // per task
		std::unique_ptr<std::atomic<bool>[]>          Parked;               // gate: parked, not dispatched
		std::atomic<std::uint32_t>                    Outstanding{ 0 };     // nodes not finished
		std::atomic<std::uint64_t>                    Serial{ 0 };          // fence value once drained
		std::atomic<std::int64_t>                     SubmitTickMs{ 0 };    // when it was submitted
		std::atomic<bool>                             bStallReported{ false };
		std::mutex                                    M;
		std::condition_variable                       Cv;
	};

	/** Submit one task's runner to its pool (release + resubmit downstreams). */
	void SubmitTaskFor(std::size_t Index, std::uint64_t Frame);

	/** Execute the task's node via the subclass protocol hook. */
	void ExecuteNodeFor(std::size_t Index);

	/** Gate: a root claims the layer or parks; the instance's sink hands it over. */
	void GateRegister(std::size_t Group, std::size_t Index, std::uint64_t Frame);
	void GateComplete(std::size_t Group, std::uint64_t Frame);

	/** Stall audit: report (once per frame) what is holding a frame that should have
	 *  drained long ago. Debug builds only. */
	void ReportStall(std::uint64_t Frame, const FFrameSlot& Slot);

	// Each FTask is heap-allocated so its fields have stable addresses.
	std::vector<std::unique_ptr<FTask>> Tasks;
	std::map<std::pair<std::string, std::type_index>, std::size_t> Lookup; // (name,stage) -> task index
	std::string CompileErrorNode;                                          // node that broke Compile

	std::vector<std::unique_ptr<FGroupGate>>  Gates;      // one per distinct group (node Name)
	std::vector<std::string>                  GroupNames;  // Gates[i]'s layer name (diagnostics)
	std::vector<std::unique_ptr<FFrameSlot>>  Ring;       // MAHO_FRAMES_IN_FLIGHT slots
	std::uint32_t                             RingDepth = MAHO_FRAMES_IN_FLIGHT;
	std::uint64_t                             NextFrame = 1;   // frame id of the next SubmitFrame
};

} // namespace Maho
