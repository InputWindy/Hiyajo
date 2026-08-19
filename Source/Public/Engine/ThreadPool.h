#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
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
		// 15 tasks on a 5-thread cap run 5+5+5 — the queue handles the batching,
		// invisible to the caller.
		EnsureThreads(static_cast<std::uint32_t>(Count));

		std::atomic<std::uint32_t> Remaining{static_cast<std::uint32_t>(Count)};
		std::mutex BarrierMutex;
		std::condition_variable BarrierCv;
		std::exception_ptr FirstError;
		std::mutex ErrorMutex;

		auto SubmitOne = [&](auto&& F)
		{
			Submit([&]
			{
				try
				{
					F();
				}
				catch (...)
				{
					// Remember the first failure but still release the barrier
					// (an exception must never leave Remaining stuck above 0).
					std::lock_guard Lock(ErrorMutex);
					if (!FirstError)
					{
						FirstError = std::current_exception();
					}
				}
				if (--Remaining == 0)
				{
					std::lock_guard Lock(BarrierMutex);
					BarrierCv.notify_all();
				}
			});
		};
		(SubmitOne(std::forward<FCallables>(Callables)), ...);

		std::unique_lock Lock(BarrierMutex);
		BarrierCv.wait(Lock, [&] { return Remaining == 0; });

		if (FirstError)
		{
			std::rethrow_exception(FirstError);
		}
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
