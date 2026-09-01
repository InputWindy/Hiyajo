#pragma once

#include <Core/Fatal.h>

#include <atomic>
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
class FThreadPool
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

inline FThreadPool::FThreadPool(std::uint32_t InNumThreads)
	: NumThreads(InNumThreads == 0 ? std::thread::hardware_concurrency() : InNumThreads)
{
	if (NumThreads == 0)
	{
		NumThreads = 1;
	}
	// Zero workers at construction -- lazily started on first Submit.
	Workers.reserve(NumThreads);
}

inline FThreadPool::~FThreadPool()
{
	{
		std::lock_guard Lock(Mutex);
		bStopping = true;
	}
	CondVar.notify_all();
	for (std::thread& Worker : Workers)
	{
		if (Worker.joinable())
		{
			Worker.join();
		}
	}
}

inline void FThreadPool::EnsureThreads(std::uint32_t Required)
{
	// Never exceed the hardware_concurrency-derived cap (more threads than
	// cores only adds contention).
	if (Required > NumThreads)
	{
		Required = NumThreads;
	}
	std::lock_guard Lock(Mutex);
	while (Workers.size() < Required)
	{
		Workers.emplace_back(&FThreadPool::WorkerLoop, this);
	}
}

inline void FThreadPool::Submit(std::function<void()> Task)
{
	EnsureThreads(1);   // lazy-start: at least one worker must exist to consume
	{
		std::lock_guard Lock(Mutex);
		Queue.push_back(std::move(Task));
		PendingCount += 1;
	}
	CondVar.notify_one();
}

inline void FThreadPool::Flush()
{
	// Wait for true quiescence: the queue empty AND PendingCount zero (a running
	// task only decrements after it COMPLETED, so zero means nothing is in
	// flight). The while-loop re-waits if a concurrent Submit bumps the count
	// while we drain -- a nested graph (e.g. the render graph dispatching its
	// downstream stages) can submit from a worker, and those must not escape.
	std::unique_lock Lock(Mutex);
	while (PendingCount != 0 || !Queue.empty())
	{
		CondVar.wait(Lock, [&] { return PendingCount == 0 && Queue.empty(); });
	}
}

inline void FThreadPool::WorkerLoop()
{
	while (true)
	{
		std::function<void()> Task;
		{
			std::unique_lock Lock(Mutex);
			CondVar.wait(Lock, [this] { return bStopping || !Queue.empty(); });
			if (bStopping && Queue.empty())
			{
				return;
			}
			Task = std::move(Queue.front());
			Queue.pop_front();
		}
		try
		{
			Task();
		}
		catch (const std::exception& E)
		{
			// A throwing task must not kill the host -- a buggy plugin stage should
			// be isolated. Report (non-fatal) and keep the worker serving.
			ReportError(E.what());
		}
		catch (...)
		{
			ReportError("Unknown exception in thread-pool worker");
		}
		{
			std::lock_guard Lock(Mutex);
			PendingCount -= 1;
			if (PendingCount == 0)
			{
				CondVar.notify_all();
			}
		}
	}
}

} // namespace Maho
