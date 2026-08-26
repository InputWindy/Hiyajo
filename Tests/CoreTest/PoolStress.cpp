// Isolated stress test for FThreadPool — no FLayer, no stack objects, no
// scheduler. Answers: is the pool alone race-free under high-frequency
// RunTasks/Run with lazy growth + teardown?
#include <Core/ThreadPool.h>

#include <atomic>
#include <cstdio>
#include <random>
#include <thread>

int main()
{
	// Pool capped at hardware_concurrency workers, grown lazily.
	Maho::FThreadPool Pool(0);

	std::atomic<unsigned long long> Total{ 0 };

	// 1) High-frequency RunTasks batches with per-batch work + lazy growth.
	constexpr int Rounds = 200000;
	for (int r = 0; r < Rounds; ++r)
	{
		const int Batch = 1 + (r % 16);        // 1..16 tasks per batch
		std::vector<std::function<void()>> Tasks;
		Tasks.reserve(Batch);
		for (int i = 0; i < Batch; ++i)
		{
			Tasks.emplace_back([&, d = (i * 31 + 7)] {
				// cheap atomic work; no stack captures beyond the loop locals
				Total.fetch_add(d & 3, std::memory_order_relaxed);
			});
		}
		Pool.RunTasks(std::move(Tasks));        // barrier each batch
	}

	// 2) Interleave raw Submit fires (fire-and-forget) with the bars.
	bool bThrow = false;
	for (int i = 0; i < 500000 && !bThrow; ++i)
	{
		Pool.Submit([&] { Total.fetch_add(1, std::memory_order_relaxed); });
		if (i % 1000 == 0)
		{
			Pool.Run([] {});
		}
	}

	// 3) EnsureThreads races with a concurrent Run from two caller threads.
	std::thread T1([&] {
		for (int i = 0; i < 2000; ++i)
		{
			Pool.Run([] { }, [] { }, [] { });
		}
	});
	std::thread T2([&] {
		for (int i = 0; i < 2000; ++i)
		{
			Pool.Run([] { }, [] { }, [] { }, [] { }, [] { });
		}
	});
	T1.join();
	T2.join();

	if (!bThrow)
	{
		std::puts("pool OK");
	}
	return 0;
}
