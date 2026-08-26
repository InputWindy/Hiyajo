// Isolated test for FQueue — a thread-safe, type-erased PENDING-command
// collection partitioned by CATALOG lane. Enqueue routes by GetCatalogId;
// Dequeue pops FIFO per lane; the queue only HOLDS commands (the consumer
// applies them). Verifies: multi-producer Enqueue, per-lane FIFO, Size/IsEmpty.
#include <Core/Queue.h>

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

using namespace Maho;

namespace
{
	// one test catalog lane
	inline constexpr std::uint64_t kIncLane = 0x1000;

	// a value command: derives ICommand, carries an increment, one FIFO lane
	struct FIncCmd : public ICommand
	{
		explicit FIncCmd(int InAmount)
			: Amount(InAmount)
		{
		}

		[[nodiscard]] std::uint64_t GetCatalogId() const override
		{
			return kIncLane;
		}

		int Amount;
	};
}

int main()
{
	FQueue Q;

	// multi-producer Enqueue — every command lands in the kIncLane FIFO
	constexpr int Producers = 4;
	constexpr int Rounds = 20000;
	std::vector<std::thread> Ts;
	for (int p = 0; p < Producers; ++p)
	{
		Ts.emplace_back([&] {
			for (int i = 0; i < Rounds; ++i)
			{
				Q.Enqueue(std::make_unique<FIncCmd>(1));
			}
		});
	}
	for (auto& T : Ts)
	{
		T.join();
	}

	if (Q.Size() != Producers * Rounds)
	{
		std::printf("[FAIL] size=%zu want=%d (no dedupe in catalog queue)\n",
			Q.Size(), Producers * Rounds);
		return 1;
	}

	// consumer drains the lane FIFO and applies each command itself
	int Applied = 0;
	while (auto Cmd = Q.Dequeue(kIncLane))
	{
		auto* Inc = static_cast<FIncCmd*>(Cmd.get());
		Applied += Inc->Amount;
	}
	if (Applied != Producers * Rounds)
	{
		std::printf("[FAIL] applied=%d want=%d\n", Applied, Producers * Rounds);
		return 1;
	}
	if (!Q.IsEmpty())
	{
		std::printf("[FAIL] drain should clear (%zu pending)\n", Q.Size());
		return 1;
	}

	// paced consumption: take a bounded number per safe point, rest stays pending
	Q.Enqueue(std::make_unique<FIncCmd>(1));
	Q.Enqueue(std::make_unique<FIncCmd>(2));
	Q.Enqueue(std::make_unique<FIncCmd>(3));
	if (Q.Size() != 3)
	{
		std::printf("[FAIL] size=%zu want=3\n", Q.Size());
		return 1;
	}
	int Sum = 0;
	for (int N = 0; N < 2; ++N)   // consume 2, one stays pending
	{
		if (auto Cmd = Q.Dequeue(kIncLane))
		{
			Sum += static_cast<FIncCmd*>(Cmd.get())->Amount;
		}
	}
	if (Sum != 3 || Q.Size() != 1)
	{
		std::printf("[FAIL] paced consume: sum=%d size=%zu want 3/1\n", Sum, Q.Size());
		return 1;
	}
	if (auto Cmd = Q.Dequeue(kIncLane))
	{
		Sum += static_cast<FIncCmd*>(Cmd.get())->Amount;   // drain the last (value 3)
	}
	if (Sum != 6 || !Q.IsEmpty())
	{
		std::printf("[FAIL] final drain: sum=%d empty=%d want 6/true\n", Sum, Q.IsEmpty());
		return 1;
	}

	std::puts("ok: FQueue catalog lanes — multi-producer, per-lane FIFO, paced drain");
	return 0;
}
