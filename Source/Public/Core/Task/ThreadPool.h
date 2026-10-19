/// @file
/// @brief The engine's threading: one pool of workers (+ lanes), and the RESIDENT ROLE that lives on a
///        lane of it.
///
/// Two shapes of the same thread set, which is why they share a header:
/// * `FThreadPool` -- N workers serving every SHARED lane. A lane is one owner's queue, so a barrier can
///   drain exactly that owner's work instead of the whole process's.
/// * `FThreadedServer` -- a PERSISTENT lane: a dedicated thread of the same pool serves it and nothing
///   else. That is what a module owning an OS handle needs (a Win32 message queue belongs to the thread
///   that created it -- see AGENTS "Thread Affinity").
///
/// The pool is the only place threads are created, named, or drained; a layer does not own a thread of
/// its own any more.
#pragma once


#include <Core/Export.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace Maho
{

/// @brief A fixed set of worker threads serving named LANES, plus one optional thread per PERSISTENT lane.
///
/// A lane is a queue an owner submits to and flushes; the pools of one process are usually just one (the
/// host's), shared by every collector through `FFrameBuilder::BindPool`.
class MAHO_API FThreadPool
{
public:
	/// @brief A lane's handle: an index into the pool's lane table.
	using FLane = std::uint32_t;

	/// @brief The lane every pool starts with (it exists from construction and is never destroyed).
	static constexpr FLane         DefaultLane = 0;

	/// @brief What `CreatePersistentLane` returns when the table is full.
	static constexpr FLane         InvalidLane = 0xFFFFFFFFu;

	/// @brief How many lanes one pool can hold.
	static constexpr std::uint32_t MaxLanes    = 16;

	/// @brief Creates the pool (threads are started lazily, on the first submission).
	/// @param NumThreads Worker count; 0 means "the machine's parallelism", overridable by the
	///        `MAHO_PARALLELISM` environment variable. Persistent lanes are NOT counted here.
	explicit FThreadPool(std::uint32_t NumThreads = 0);

	/// @brief Stops every worker and every persistent lane (draining what they hold first).
	~FThreadPool();

	/// @brief Not copyable: the pool owns its threads and its lanes.
	FThreadPool(const FThreadPool&) = delete;

	/// @brief Not copy-assignable: the pool owns its threads and its lanes.
	FThreadPool& operator=(const FThreadPool&) = delete;

	/// @brief A lane is one OWNER's queue -- one per layer that submits its own work (the engine host, a
	/// graph), so a barrier can drain exactly that owner's tasks. There is deliberately no lane-less
	/// spelling of Submit/Flush: "whose work is this?" is the question the barrier exists to answer, and
	/// a default lane answers it for everyone but DefaultLane's owner.
	/// @return A fresh lane (at capacity the caller is given `DefaultLane`, and it is reported).
	[[nodiscard]] FLane CreateLane();

	/// @brief Releases a lane. Refused (and reported) while it still has work.
	/// @param Lane The lane to release (`DefaultLane` is ignored: it is never destroyed).
	void DestroyLane(FLane Lane);

	/// @brief Creates a lane with its OWN dedicated thread: that thread consumes ONLY this lane, and the
	///        shared workers never take from it.
	///
	/// This is what THREAD AFFINITY needs -- a module that owns an OS handle (a Win32 window's message
	/// queue lives on the thread that created it) cannot have its work run by whichever worker happens to
	/// be free (see AGENTS "Thread Affinity"). It is the pool-backed form of "this layer owns a resident
	/// thread", and it replaces the per-layer `std::thread` + queue `FThreadedServer` used to carry.
	/// @param ThreadName A label for traces and debuggers (not an OS thread name).
	/// @return The lane, or `InvalidLane` when the table is full (the caller must then fail loudly --
	///         falling back to a shared lane would silently break the affinity it was asked for).
	[[nodiscard]] FLane CreatePersistentLane(const char* ThreadName);

	/// @brief Releases a persistent lane: its thread drains what is queued and then exits.
	/// @param Lane The lane to release.
	void DestroyPersistentLane(FLane Lane);

	/// @brief True when the CALLING thread is the one pinned to `Lane` (persistent lanes only).
	///
	/// This replaces a server's private thread-id check: a module can ask "am I on my own thread?"
	/// without owning one.
	/// @param Lane The lane to ask about.
	/// @return True when the caller is that lane's thread.
	[[nodiscard]] bool IsCurrentThread(FLane Lane) const;

	/// @brief Enqueues onto a lane and returns immediately.
	/// @param Lane Where to queue (`DefaultLane` exists from construction).
	/// @param Task What to run; it runs on a worker, or on the lane's own thread when it is persistent.
	void Submit(FLane Lane, std::function<void()> Task);

	/// @brief Quiescence barrier for ONE lane: blocks until that lane's queue is empty and nothing is
	///        running from it. Tolerates concurrent `Submit` -- a nested graph may add work while we wait.
	///
	/// A caller already running INSIDE this pool may help drain the lane, so a barrier never deadlocks on
	/// work it could have done itself; a PERSISTENT lane is never helped (the whole point of pinning it is
	/// that only its own thread runs its work).
	/// @param Lane The lane to drain.
	void Flush(FLane Lane);

	/// @brief How many workers the pool runs (persistent lanes excluded).
	/// @return The worker count.
	[[nodiscard]] std::uint32_t GetNumThreads() const;

	/// @brief How much work a lane still holds (queued + running).
	/// @param Lane The lane to ask about.
	/// @return The pending count (0 for a lane that does not exist).
	[[nodiscard]] std::uint32_t GetLanePending(FLane Lane) const;

private:
	/// @brief The pool's hidden state (workers, lanes, the barrier bookkeeping).
	struct FImpl;

	/// @brief The hidden state (see FImpl).
	std::unique_ptr<FImpl> Impl;
};

// ── FThreadedServer: a RESIDENT role on that pool ───────────────────────────────
//
// One lane of the pool, served by a thread of its own, FIFO, drained by Flush. It lives in THIS header
// because it is the pool's other half, not a separate facility: a lane is either SHARED (workers take
// it) or PERSISTENT (a resident role owns it), and both are shapes of the same thread set.
//
// The thread is the point. A module that owns an OS handle -- a Win32 window's message queue belongs
// to the thread that created it -- cannot have its work run by whichever worker happens to be free
// (see AGENTS "Thread Affinity"), so its lane is PERSISTENT: `CreatePersistentLane` gives it a
// dedicated thread that serves that lane and nothing else. What this class adds on top is the role's
// lifecycle (OnInitialize/OnShutdown), its name for traces, and "am I on my own thread?".
//
// The pool is INJECTED -- from the consumer's side this is "hand my backend to that pool": a server is
// a collector's sibling, so it binds to the HOST's worker set instead of keeping one of its own.
// `BindPool(Engine.GetPool())` before Initialize, the same shape `FFrameBuilder::BindPool` already has.
// Without it the server owns a pool (standalone use: a test, a tool).
/// @brief A resident role: its own lane (and thread) on a pool, with a lifecycle.
class MAHO_API FThreadedServer
{
public:
	/// @brief Creates the role (no lane yet: `Initialize` starts it).
	FThreadedServer();

	/// @brief Shuts the role down if it is still running.
	virtual ~FThreadedServer();

	/// @brief Not copyable: a role owns a lane.
	FThreadedServer(const FThreadedServer&) = delete;

	/// @brief Not copy-assignable: a role owns a lane.
	FThreadedServer& operator=(const FThreadedServer&) = delete;

	/// @brief Binds this role's execution to `InPool` -- the backend a server hands over instead of
	///        running a thread set of its own. Must be called before `Initialize`.
	/// @param InPool The pool whose lane this role will live on (usually `Engine.GetPool()`).
	void BindPool(FThreadPool& InPool);

	/// @brief Starts the resident lane (named `GetThreadName()`); then work can be queued.
	/// @return False when the lane could not be obtained (the pool is at lane capacity), in which case
	///         the role is left stopped rather than silently sharing another owner's lane.
	bool Initialize();

	/// @brief Drains the lane, releases it (its thread exits after the queue is empty), then `OnShutdown`.
	///
	/// Virtual because a role may need its own teardown ORDERING around the lane: a role whose work runs
	/// elsewhere (a graph's node bodies run on the pool's work lane) has to wait for that work BEFORE the
	/// lane goes away -- afterwards its notifications would have nowhere to arrive.
	virtual void Shutdown();

	/// @brief True between a successful `Initialize` and `Shutdown`.
	/// @return Whether the role is live.
	[[nodiscard]] bool IsRunning() const;

	/// @brief True when the CALLING thread is this role's thread.
	/// @return Whether the caller may touch the role's thread-affine state directly.
	[[nodiscard]] bool IsServerThread() const;

protected:
	/// @brief Queues work for this role's thread. Refused (and REPORTED) unless `Initialize` has run:
	///        a task accepted by a stopped role would simply never run, which is the hardest bug to see.
	///
	/// PROTECTED, not public: the lane is the role's internals. An outside caller must speak the ROLE's
	/// verbs (`RunOnPlatformThread`, `Import`, `FTaskGraph::Submit(desc, predecessors)`), never post a raw
	/// closure at it -- that would let any module inject work into another module's thread-affine state.
	/// (It also keeps a derived role's own `Submit` overload from clashing with this one; one that shares
	/// the name calls `FThreadedServer::Submit(...)` explicitly.)
	/// @param Task What to run on the role's thread.
	void Submit(std::function<void()> Task);

	/// @brief Blocks until the lane has drained -- the role's own quiescence point.
	void Flush();

	/// @brief Runs on the CALLER's thread, before the lane starts. Returning false aborts `Initialize`.
	/// @return Whether the role came up.
	[[nodiscard]] virtual bool OnInitialize();

	/// @brief Runs after the lane has drained and been released (on the caller's thread).
	virtual void OnShutdown();

	/// @brief The lane's name: what a trace calls this role's row, and what the lane is registered as.
	/// @return A literal that outlives the role.
	[[nodiscard]] virtual const char* GetThreadName() const;

private:
	/// @brief The role's hidden state (the pool it bound to, its lane, whether it runs).
	struct FImpl;

	/// @brief The hidden state (see FImpl).
	std::unique_ptr<FImpl> Impl;
};

}
