// Compile+run check for FParallelScheduler::ForEachSingletons — drives CRTP
// singletons level-by-level (compile-time topo via MAHO_EXTEND_DEPS), each
// T::Get() singleton handed to the visitor.
#include <Core/Extension.h>
#include <Core/Singleton.h>
#include <Engine/Layer.h>
#include <Engine/Schedulers.h>

#include <atomic>
#include <cstdio>

using namespace Maho;

namespace
{
	std::atomic<int> gPolls{ 0 };

	// two singleton plugin layers; B depends on A → A is level 0, B level 1
	struct FA : public TSingleton<FA>, public IPlugin<IInitialize, IShutdown>
	{
		static FA& Get() { static FA I; return I; }   // test-local: no DLL boundary
		using FDepends = TTypeList<FDefaultSlot, TTypeList<>>;
		int InitCount = 0;
		void Initiate(int, char**) override { ++InitCount; }
		void Shutdown() override {}
		void Poll() { gPolls.fetch_add(1, std::memory_order_relaxed); }
	};

	struct FB : public TSingleton<FB>, public IPlugin<IInitialize, IShutdown>
	{
		static FB& Get() { static FB I; return I; }
		using FDepends = TTypeList<FDefaultSlot, TTypeList<FA>>;
		int InitCount = 0;
		void Initiate(int, char**) override { ++InitCount; }
		void Shutdown() override {}
		void Poll() { gPolls.fetch_add(1, std::memory_order_relaxed); }
	};

	// FC has no topo deps (leaf) — another root-level singleton
	struct FC : public TSingleton<FC>, public IPlugin<IInitialize, IShutdown>
	{
		static FC& Get() { static FC I; return I; }
		int InitCount = 0;
		void Initiate(int, char**) override { ++InitCount; }
		void Shutdown() override {}
		void Poll() { gPolls.fetch_add(1, std::memory_order_relaxed); }
	};

	using FSingletons = TTypeList<FA, FB, FC>;
	using FScheduler = Parallel::FParallelScheduler;
}

int main()
{
	FScheduler Sched;
	Sched.ForEach<FSingletons>([](auto& S) { S.Poll(); });
	if (gPolls.load() != 3)
	{
		std::printf("[FAIL] polled=%d want=3\n", gPolls.load());
		return 1;
	}

	// the TTypeQuery sugar: Select keeps matching types, ForEach drives their
	// singletons (T::Get()) topo-leveled — same table, filterable.
	gPolls.store(0);
	Parallel::TypeQuery<FSingletons>()
		.Select<IPlugin<IInitialize, IShutdown>>()
		.ForEach([](auto& S) { S.Poll(); });
	if (gPolls.load() != 3)
	{
		std::printf("[FAIL] TTypeQuery polled=%d want=3\n", gPolls.load());
		return 1;
	}

	// TTypeQuery.Select().ForEach also drives the fixed lifecycle (Initiate/Shutdown)
	Parallel::TypeQuery<FSingletons>()
		.Select<IPlugin<IInitialize, IShutdown>>()
		.ForEach([](auto& S) { S.Initiate(0, nullptr); });
	if (FA::Get().InitCount != 1 || FB::Get().InitCount != 1 || FC::Get().InitCount != 1)
	{
		std::puts("[FAIL] singletons not Initiate'd exactly once");
		return 1;
	}

	std::puts("ok: ForEachSingletons + TTypeQuery.Select().ForEach drove singletons");
	return 0;
}
