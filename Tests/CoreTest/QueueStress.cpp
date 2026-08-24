// Isolated test for FQueue<TCommand> — a thread-safe PENDING VALUE-command
// collection. Enqueue/Dequeue are deduped requests; Flush executes-and-clears.
// Verifies: multi-producer Enqueue, dedupe, Flush runs each command once.
#include <Core/Queue.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace Maho;

namespace
{
	std::atomic<int> gExec{ 0 };

	// a value command: copyable + comparable + executable
	struct FIncCmd
	{
		int Amount;
		bool operator==(const FIncCmd& O) const { return Amount == O.Amount; }
		void Execute() { gExec.fetch_add(Amount, std::memory_order_relaxed); }
	};
}

int main()
{
	FQueue<FIncCmd> Q;

	// multi-producer Enqueue, heavily duplicated values (dedupe kicks in)
	constexpr int Producers = 4;
	constexpr int Rounds = 20000;
	std::vector<std::thread> Ts;
	for (int p = 0; p < Producers; ++p)
	{
		Ts.emplace_back([&] {
			for (int i = 0; i < Rounds; ++i)
			{
				Q.Enqueue(FIncCmd{ 1 }); // same value → deduped to one
			}
		});
	}
	for (auto& T : Ts)
	{
		T.join();
	}

	// all producers pushed the identical value — queue holds exactly one
	if (Q.Size() != 1)
	{
		std::printf("[FAIL] size=%zu want=1 (dedupe)\n", Q.Size());
		return 1;
	}

	Q.Flush(); // execute one command once
	if (gExec.load() != 1)
	{
		std::printf("[FAIL] executed=%d want=1\n", gExec.load());
		return 1;
	}
	if (!Q.IsEmpty())
	{
		std::printf("[FAIL] Flush should clear (%zu pending)\n", Q.Size());
		return 1;
	}

	// distinct values are not deduped
	Q.Enqueue(FIncCmd{ 1 });
	Q.Enqueue(FIncCmd{ 2 });
	if (Q.Size() != 2)
	{
		std::printf("[FAIL] size=%zu want=2 (distinct)\n", Q.Size());
		return 1;
	}
	Q.Dequeue(FIncCmd{ 1 }); // remove one pending
	if (Q.Size() != 1)
	{
		std::printf("[FAIL] size=%zu want=1 after dequeue\n", Q.Size());
		return 1;
	}
	Q.Flush();
	if (gExec.load() != 3) // 1 (earlier) + 2 (the {2} that survived)
	{
		std::printf("[FAIL] executed=%d want=3\n", gExec.load());
		return 1;
	}

	// paced flush: consumer takes at most N per safe point, rest stays pending
	gExec.store(0);
	Q.Enqueue(FIncCmd{ 1 });
	Q.Enqueue(FIncCmd{ 2 });
	Q.Enqueue(FIncCmd{ 3 });
	Q.Enqueue(FIncCmd{ 4 });
	Q.Enqueue(FIncCmd{ 5 });
	if (Q.Size() != 5)
	{
		std::printf("[FAIL] size=%zu want=5\n", Q.Size());
		return 1;
	}
	Q.Flush(2); // consume 2 (values 1,2), 3 remain pending
	if (gExec.load() != 3 || Q.Size() != 3)
	{
		std::printf("[FAIL] paced flush: exec=%d size=%zu want 3/3\n", gExec.load(), Q.Size());
		return 1;
	}
	Q.Flush(); // drain the rest (values 3,4,5 → +3+4+5=12, total 15)
	if (gExec.load() != 15 || !Q.IsEmpty())
	{
		std::printf("[FAIL] drain: exec=%d size=%zu want 15/empty\n", gExec.load(), Q.Size());
		return 1;
	}

	std::puts("ok: FQueue value commands — dedupe, dequeue, flush-and-clear, paced flush");
	return 0;
}
