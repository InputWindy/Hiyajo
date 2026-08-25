#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Maho
{

/**
 * Fixed-size thread pool (persistent workers + task queue + barrier).
 *
 * Submit: enqueue one fire-and-forget task.
 * Run: run every callable concurrently and block until all complete.
 *
 * Lazily growable via EnsureThreads — the parallel scheduler sizes the pool
 * to the extension count. Tasks must be thread-safe — they run concurrently
 * on distinct workers.
 */
class FThreadPool
{
public:
	/** NumThreads = 0 → std::thread::hardware_concurrency(). */
	explicit FThreadPool(std::uint32_t NumThreads = 0);
	~FThreadPool();

	FThreadPool(const FThreadPool&) = delete;
	FThreadPool& operator=(const FThreadPool&) = delete;

	/** Enqueue one task; returns immediately. */
	void Submit(std::function<void()> Task);

	/** Grow the pool to at least Required workers (lazy — never shrinks). */
	void EnsureThreads(std::uint32_t Required);

	/**
	 * Shared barrier state for one batch. Heap-allocated so every worker task
	 * holds a shared_ptr to it — the barrier stays alive as long as any task can
	 * still reach it, never tied to the caller's stack frame (a pooled worker may
	 * run a task after the submitting Run() has returned).
	 */
	struct FBarrier
	{
		std::mutex Mutex;
		std::condition_variable Cv;
		std::atomic<std::uint32_t> Remaining;
		explicit FBarrier(std::uint32_t InRemaining)
			: Remaining(InRemaining)
		{
		}
	};

	/** Run every callable on the pool; block until all complete (barrier). */
	template <typename... FCallables>
	void Run(FCallables&&... Callables)
	{
		constexpr std::size_t Count = sizeof...(Callables);
		if (Count == 0)
		{
			return;
		}
		if (Count == 1)
		{
			(Callables(), ...);
			return;
		}

		// Lazy-start: grow the group to the workload (capped by NumThreads).
		EnsureThreads(static_cast<std::uint32_t>(Count));

		auto Bar = std::make_shared<FBarrier>(static_cast<std::uint32_t>(Count));

		auto SubmitOne = [&](auto&& F)
		{
			Submit([&, Bar]
			{
				try
				{
					F();
				}
				catch (...)
				{
					// Swallow for the barrier only; the caller re-checks per-task
					// errors after the batch below.
				}
				if (--Bar->Remaining == 0)
				{
					std::lock_guard Lock(Bar->Mutex);
					Bar->Cv.notify_all();
				}
			});
		};
		(SubmitOne(std::forward<FCallables>(Callables)), ...);

		std::unique_lock Lock(Bar->Mutex);
		Bar->Cv.wait(Lock, [&] { return Bar->Remaining == 0; });
	}

	/** Run a runtime-length list of callables concurrently; block until done. */
	void RunTasks(std::vector<std::function<void()>> Tasks)
	{
		const std::size_t Count = Tasks.size();
		if (Count == 0)
		{
			return;
		}
		if (Count == 1)
		{
			Tasks[0]();
			return;
		}

		EnsureThreads(static_cast<std::uint32_t>(Count));

		auto Bar = std::make_shared<FBarrier>(static_cast<std::uint32_t>(Count));

		for (auto& Task : Tasks)
		{
			Submit([Bar, Task = std::move(Task)]() mutable
			{
				try
				{
					Task();
				}
				catch (...)
				{
					// Swallow; the batch call must not wedge the barrier.
				}
				if (--Bar->Remaining == 0)
				{
					std::lock_guard Lock(Bar->Mutex);
					Bar->Cv.notify_all();
				}
			});
		}

		std::unique_lock Lock(Bar->Mutex);
		Bar->Cv.wait(Lock, [&] { return Bar->Remaining == 0; });
	}

	[[nodiscard]] std::uint32_t GetNumThreads() const
	{
		return NumThreads;
	}

private:
	void WorkerLoop();

	std::vector<std::thread> Workers;
	std::deque<std::function<void()>> Queue;
	std::mutex Mutex;
	std::condition_variable CondVar;
	std::uint32_t NumThreads;
	bool bStopping = false;
};

inline FThreadPool::FThreadPool(std::uint32_t InNumThreads)
	: NumThreads(InNumThreads == 0 ? std::thread::hardware_concurrency() : InNumThreads)
{
	if (NumThreads == 0)
	{
		NumThreads = 1;
	}
	// Zero workers at construction — the scheduler grows the group lazily on
	// the first ForEach (EnsureThreads). NumThreads is the hard upper bound.
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

inline void FThreadPool::Submit(std::function<void()> Task)
{
	{
		std::lock_guard Lock(Mutex);
		Queue.push_back(std::move(Task));
	}
	CondVar.notify_one();
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
	}
}

} // namespace Maho
