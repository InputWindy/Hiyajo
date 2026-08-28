#pragma once

#include <Core/ThreadPool.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace Maho
{

/** Named dep of a node: { dep object name, dep's stage }. */
struct FTaskGraphDependency
{
	std::string     Name;
	std::type_index Stage = std::type_index(typeid(void));   // void = unset
};

/** Base task-graph node — pure topology. Subclasses extend it with the execution payload. */
struct FTaskGraphNode
{
	std::string                         Name;          // object identity (e.g. "World")
	std::type_index                     Stage = std::type_index(typeid(void));  // void = unset
	std::vector<FTaskGraphDependency>  Dependencies;   // edges: (name, stage) → this node
};

/**
 * Dependency-graph scheduler. Schedules NODES, where a node is an (object,
 * stage) pair. Edges come from each node's dependency tuples. A node is ready
 * when ALL of its direct dependencies have completed — the graph is
 * stage-agnostic, so a node whose deps finished is released immediately (no
 * stage barrier — this enables cross-stage pipelining).
 *
 * Lifecycle:
 *   Init     — load the full node set (topology data only)
 *   Compile  — wire edges + detect cycles/missing deps → bool
 *   Execute  — topological dispatch (async, submits ready nodes to the pool)
 *   Flush    — block until the graph drains
 *
 * Execution protocol is delegated to subclasses via ExecuteNode(): the base
 * FTaskGraphNode carries only {Name, Stage, Dependencies}; a subclass defines
 * its own node (holding whatever callback/context it needs) and casts it back
 * inside ExecuteNode.
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
	virtual ~FTaskGraph() = default;

	/** ① Load the full node set (topology data only). */
	void Init(std::vector<FNode*> Nodes);

	/** ② Wire edges + validate (cycle / missing dep). Returns false on error. */
	bool Compile();

	/** ③ Dispatch ready nodes to the pool (async — call Flush to sync). */
	void Execute();

	/** ④ Block until every submitted task completed (graph drained). */
	void Flush();

	/** Re-run the current compiled graph (resets per-node pending counts). */
	void Reset();

protected:
	/**
	 * Execution protocol hook — the base graph only knows a node is ready; the
	 * subclass casts FNode to its own derived node and drives the callback.
	 * Runs on a pool worker thread (must be thread-safe).
	 */
	virtual void ExecuteNode(FNode* Node) = 0;

	FThreadPool& Pool;

private:
	struct FTask
	{
		FNode* Node = nullptr;
		std::vector<std::size_t> Downstreams;          // task indices this one releases
		std::uint32_t InitPending = 0;                 // compiled initial dep count
		std::uint32_t Pending = 0;                     // live dep count (Mutex-guarded)
	};

	/** Submit one task's runner to the pool (release + resubmit downstreams). */
	void SubmitTask(std::size_t Index);

	/** Execute the task's node via the subclass protocol hook. */
	void ExecuteNodeFor(std::size_t Index);

	std::vector<FTask> Tasks;                                              // all nodes
	std::map<std::pair<std::string, std::type_index>, std::size_t> Lookup; // (name,stage)→task index
	std::uint32_t Remaining = 0;                                           // tasks not yet completed (Mutex-guarded)
	std::mutex Mutex;
};

} // namespace Maho
