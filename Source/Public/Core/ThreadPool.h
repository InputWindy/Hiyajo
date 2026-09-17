#pragma once

// ThreadPool -- fixed-size thread pool (persistent workers + FIFO task queue).
// The implementation lives in Source/Private/Core/ThreadPool.cpp: a pool is a
// MEMBER of every plugin that installs a collector (FFrameBuilder::Pool), so the
// class is exported and its methods are called across the DLL boundary rather
// than inlined per module.

#include <Core/Export.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Maho
{

/**
 * Fixed-size thread pool (persistent workers + FIFO task queue).
 *
 * Submit:   enqueue one task (returns immediately; runs out of order on workers).
 * Flush:    lockstep barrier -- block until every task submitted SO FAR completed.
 *
 * Workers are lazily started on the first Submit (never shrink). Tasks must be
 * thread-safe -- they run concurrently on distinct workers.
 */
class MAHO_API FThreadPool
{
public:
	/** NumThreads = 0 -> std::thread::hardware_concurrency(). */
	explicit FThreadPool(std::uint32_t NumThreads = 0);
	~FThreadPool();

	FThreadPool(const FThreadPool&) = delete;
	FThreadPool& operator=(const FThreadPool&) = delete;

	/** Enqueue one task; returns immediately. Lazily starts a worker if needed. */
	void Submit(std::function<void()> Task);

	/**
	 * Quiescence barrier: block until the pool is TRULY idle -- every task
	 * submitted before the call, AND any task a concurrent worker submits while
	 * draining (nested/dependent graph work), has completed. Unlike a FIFO
	 * no-op barrier this tolerates Submit during the flush: the loop re-waits
	 * whenever PendingCount goes back up, so no task can escape the barrier and
	 * run later (e.g. race the engine's shutdown graph).
	 */
	void Flush();

	[[nodiscard]] std::uint32_t GetNumThreads() const
	{
		return NumThreads;
	}

private:
	/** Grow the pool to at least Required workers (lazy -- never shrinks). */
	void EnsureThreads(std::uint32_t Required);

	void WorkerLoop();

	std::vector<std::thread> Workers;
	std::deque<std::function<void()>> Queue;
	std::mutex Mutex;
	std::condition_variable CondVar;
	std::uint32_t NumThreads;
	std::uint32_t PendingCount = 0;   // uncompleted tasks (Flush waits for 0)
	bool bStopping = false;
};

} // namespace Maho
