// Compile+run check for FParallelScheduler::ForEachSingletons — drives CRTP
// singletons level-by-level (compile-time topo via MAHO_EXTEND_DEPS), each
// T::Get() singleton handed to the visitor.
#include <Core/Extension.h>
#include <Engine/Schedulers.h>

#include <atomic>
#include <cstdio>

using namespace Maho;

namespace
{
	std::atomic<int> gPolls{ 0 };

	// two singleton plugin layers; B depends on A → A is level 0, B level 1
	struct FA : public TSingleton<FA>
	{
		MAHO_EXTEND_DEPS((FDefaultSlot, FNoParent));
		void Poll() { gPolls.fetch_add(1, std::memory_order_relaxed); }
	};

	struct FB : public TSingleton<FB>
	{
		MAHO_EXTEND_DEPS((FDefaultSlot, FNoParent, FA));
		void Poll() { gPolls.fetch_add(1, std::memory_order_relaxed); }
	};

	// FC has no topo deps (leaf) — another root-level singleton
	struct FC : public TSingleton<FC>
	{
		void Poll() { gPolls.fetch_add(1, std::memory_order_relaxed); }
	};

	using FSingletons = TTypeList<FA, FB, FC>;
	using FScheduler = Parallel::FParallelScheduler<FSingletons>;
}

int main()
{
	FScheduler Sched;
	Sched.ForEachSingletons([](auto& S) { S.Poll(); });
	if (gPolls.load() != 3)
	{
		std::printf("[FAIL] polled=%d want=3\n", gPolls.load());
		return 1;
	}

	// the TTypeQuery sugar: Select keeps matching types, ForEach drives their
	// singletons (T::Get()) topo-leveled — same table, filterable.
	gPolls.store(0);
	Parallel::TypeQuery<FSingletons>()
		.Select<ISingleton>()
		.ForEach([](auto& S) { S.Poll(); });
	if (gPolls.load() != 3)
	{
		std::printf("[FAIL] TTypeQuery polled=%d want=3\n", gPolls.load());
		return 1;
	}

	std::puts("ok: ForEachSingletons + TTypeQuery.Select().ForEach drove singletons");
	return 0;
}
