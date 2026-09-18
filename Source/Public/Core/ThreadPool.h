#pragma once

// FThreadPool -- the engine's task execution primitive: a set of worker threads
// plus one or more LANES. A lane is an independent queue + completion count over
// the SAME workers:
//
//   Pool.Submit(Task);          // default lane
//   Pool.Flush();               // quiescence barrier on the default lane
//
//   const FLane L = Pool.CreateLane();
//   Pool.Submit(L, Task);
//   Pool.Flush(L);              // quiescence barrier on lane L
//
// The implementation lives in Source/Private/Core/ThreadPool.cpp: a pool is a
// MEMBER of the frame builder that installs a collector (FFrameBuilder), so the
// class is exported and its methods are called across the DLL boundary rather
// than inlined per module.
//
// WHY LANES. The engine needs one barrier per frame COLLECTOR: a collector's
// Wait() must drain the work IT submitted without waiting for another
// collector's (that is what makes "a node body blocks on another graph's
// quiescence point" safe). Giving each collector a pool of its own delivers
// that, but it multiplies THREADS by the number of collectors -- four
// collectors x hardware_concurrency is 96 threads on a 24-core machine, to run
// ~2 nodes at a time (measured). A lane separates the two things that were
// conflated: the worker set is shared, the queue and the count are per lane.
//
// Lanes also remove a trap that sharing would otherwise create. A barrier
// cannot be called from a thread that its own count includes -- Flush waits for
// zero, and the caller's task does not complete until Flush returns. A frame's
// stage bodies are dispatched by their PARENT collector's graph, so a collector
// flushing its own lane from inside a stage body is a CROSS-LANE flush, exactly
// like flushing another pool used to be. (Which is why Wait() never had to
// reason about this: it works for the same reason it worked before.)
//
// Flush() is a barrier, not a FIFO no-op: the loop re-waits whenever the count
// goes back up, so a task submitted by a worker while draining (nested graph
// work) cannot escape and run later -- e.g. race the engine's shutdown graph.
// A caller that is a WORKER of the pool it is flushing also helps drain that
// lane, because a pool whose workers are all blocked here has a queue nobody
// can run and the count would never reach zero.

#include <Core/Export.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Maho
{

class MAHO_API FThreadPool
{
public:
	/** A task lane. 0 is the pool's own; other lanes are handed out by CreateLane. */
	using FLane = std::uint32_t;

	static constexpr FLane         DefaultLane = 0;
	static constexpr FLane         InvalidLane = 0xFFFFFFFFu;
	static constexpr std::uint32_t MaxLanes    = 16;

	/** NumThreads = 0 -> std::thread::hardware_concurrency().
	 *
	 *  Workers are lazily started on the first Submit (never shrink). Tasks must be
	 *  thread-safe -- they run concurrently on distinct workers. */
	explicit FThreadPool(std::uint32_t NumThreads = 0);
	~FThreadPool();

	FThreadPool(const FThreadPool&) = delete;
	FThreadPool& operator=(const FThreadPool&) = delete;

	/** Enqueue one task on the default lane; returns immediately. Lazily starts a worker. */
	void Submit(std::function<void()> Task);

	/** Quiescence barrier on the default lane: block until every task submitted to it before
	 *  this call -- and any task a worker submits to it while draining -- has completed. */
	void Flush();

	/** Start a new lane (its own queue + count) over the same workers. Returns DefaultLane when
	 *  the pool is at MaxLanes, which is REPORTED: the caller then shares the default barrier
	 *  (correct, just not isolated) instead of silently getting a lane nobody can reason about. */
	[[nodiscard]] FLane CreateLane();

	/** Release a lane. Refused + reported when it still has work, so a caller can never hand back
	 *  a lane whose tasks are about to be counted against someone else. Lane 0 is never released. */
	void DestroyLane(FLane Lane);

	/** Enqueue on a specific lane. An unknown lane is REPORTED and falls back to the default --
	 *  work is never silently dropped. */
	void Submit(FLane Lane, std::function<void()> Task);

	/** Quiescence barrier on one lane. See Flush(). */
	void Flush(FLane Lane);

	[[nodiscard]] std::uint32_t GetNumThreads() const
	{
		return NumThreads;
	}

	/** Diagnostics / tests: uncompleted tasks on a lane (0 for a lane that does not exist). */
	[[nodiscard]] std::uint32_t GetLanePending(FLane Lane) const;

private:
	/** Grow the pool to at least Required workers (lazy -- never shrinks). */
	void EnsureThreads(std::uint32_t Required);

	void WorkerLoop();

	/** Run one task with the worker's error isolation, and mark this thread as a worker of THIS
	 *  pool for the duration (that is what licenses Flush to help drain). */
	void RunTaskSafely(const std::function<void()>& Task);

	/** Pop one task from any lane that has work (round-robin, so no lane starves). False when
	 *  every lane is empty. */
	bool TakeAnyTaskLocked(std::function<void()>& OutTask, FLane& OutLane);

	/** True when any lane has a queued task (the worker loop's wake condition). */
	[[nodiscard]] bool AnyLaneHasWorkLocked() const;

	/** Wake whoever can make progress after a lane gained a task. */
	void NotifyWorkAvailableLocked();

	/** Wake whoever can make progress after a lane lost a pending task. */
	void NotifyProgressLocked();

	struct FLaneState
	{
		std::deque<std::function<void()>> Queue;
		std::uint32_t                     Pending = 0;   // uncompleted tasks in this lane
		bool                              bInUse  = false;
	};

	std::vector<std::thread> Workers;

	/** One plain mutex guards every lane's queue, its counter and the wake bookkeeping: the
	 *  critical sections are a push and a pop, so a per-lane lock would buy nothing and cost the
	 *  "which lane has work" scan its atomicity. Never held while a task runs (RunTaskSafely is
	 *  called with it released). */
	mutable std::mutex      Mutex;
	std::condition_variable CondVar;

	std::uint32_t NumThreads;
	bool          bStopping = false;

	/** Lane 0 is the pool's own and is always in use. */
	std::array<FLaneState, MaxLanes> Lanes{};

	/** Round-robin cursor: the lane after this one is served first. */
	FLane NextLaneToServe = DefaultLane;

	/** Threads currently blocked in Flush. Lets Submit pick notify_all (a waiter may need to
	 *  help drain) instead of paying a thundering herd on every enqueue. */
	std::uint32_t FlushWaiters = 0;
};

} // namespace Maho
