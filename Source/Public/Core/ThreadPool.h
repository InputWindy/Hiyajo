#pragma once

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
 * Flush:    lockstep barrier — block until every task submitted SO FAR completed.
 *
 * Workers are lazily started on the first Submit (never shrink). Tasks must be
 * thread-safe — they run concurrently on distinct workers.
 */
class FThreadPool
{
public:
	/** NumThreads = 0 → std::thread::hardware_concurrency(). */
	explicit FThreadPool(std::uint32_t NumThreads = 0);
	~FThreadPool();

	FThreadPool(const FThreadPool&) = delete;
	FThreadPool& operator=(const FThreadPool&) = delete;

	/** Enqueue one task; returns immediately. Lazily starts a worker if needed. */
	void Submit(std::function<void()> Task);

	/**
	 * Barrier (lockstep) over the batch of tasks submitted SO FAR. The pool
	 * runs tasks out of order (multiple workers); this waits until every task
	 * submitted before the call has COMPLETED — not merely dequeued.
	 *
	 * The caller must not Submit new tasks concurrently with Flush.
	 */
	void Flush();

	[[nodiscard]] std::uint32_t GetNumThreads() const
	{
		return NumThreads;
	}

private:
	/** Grow the pool to at least Required workers (lazy — never shrinks). */
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
	// Zero workers at construction — lazily started on first Submit.
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
	// The no-op barrier task is queued after every previously submitted task;
	// WorkerLoop drains PendingCount only after a task COMPLETED, so waiting
	// for zero means all prior tasks truly finished (not just dequeued).
	Submit([]{});
	std::unique_lock Lock(Mutex);
	CondVar.wait(Lock, [&] { return PendingCount == 0; });
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
		Task();
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
