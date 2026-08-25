// Compile+run check for FLayerBase::ForEachSingleton — drives CRTP singletons
// level-by-level (compile-time topo via FDepends), each T::Get() singleton
// handed to the visitor; the type filter is Core::Query.
#include <Core/Query.h>
#include <Core/Singleton.h>
#include <Engine/Layer.h>
#include <Core/Schedulers.h>

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
}

int main()
{
	// Drive singletons level-by-level via FLayerBase::ForEachSingleton (sunk from
	// the removed TTypeQuery; the filter is Core::Query's type-agnostic Select).
	FLayerBase Base;

	Base.ForEachSingleton<FA, FB, FC>([](auto& S) { S.Poll(); });
	if (gPolls.load() != 3)
	{
		std::printf("[FAIL] polled=%d want=3\n", gPolls.load());
		return 1;
	}

	// filter the table first (Core::Query::Select keeps matching types), then drive
	gPolls.store(0);
	using FSelected = typename Maho::Query<FSingletons>().Select<IPlugin<IInitialize, IShutdown>>().FResult;
	Base.ForEachSingletonList<FSelected>([](auto& S) { S.Poll(); });
	if (gPolls.load() != 3)
	{
		std::printf("[FAIL] filtered singletons polled=%d want=3\n", gPolls.load());
		return 1;
	}

	// drive the fixed lifecycle (Initiate/Shutdown)
	Base.ForEachSingletonList<FSelected>([](auto& S) { S.Initiate(0, nullptr); });
	if (FA::Get().InitCount != 1 || FB::Get().InitCount != 1 || FC::Get().InitCount != 1)
	{
		std::puts("[FAIL] singletons not Initiate'd exactly once");
		return 1;
	}

	std::puts("ok: ForEachSingleton + Query.Select drove singletons");
	return 0;
}
