// Compile+run check for FLayer::ForEach<FLevels> singleton branch — drives CRTP
// singletons level-by-level (compile-time topo via FDepends), each T::Get()
// singleton handed to the visitor as T&; the type filter is Core::Query.
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
	struct FA : public TSingleton<FA>, public IPlugin<IInit, IShutdown>
	{
		static FA& Get() { static FA I; return I; }   // test-local: no DLL boundary
		using FDepends = TTypeList<FDefaultSlot, TTypeList<>>;
		int InitCount = 0;
		void Initialize(int, char**) override { ++InitCount; }
		void Shutdown() override {}
		void Poll() { gPolls.fetch_add(1, std::memory_order_relaxed); }
	};

	struct FB : public TSingleton<FB>, public IPlugin<IInit, IShutdown>
	{
		static FB& Get() { static FB I; return I; }
		using FDepends = TTypeList<FDefaultSlot, TTypeList<FA>>;
		int InitCount = 0;
		void Initialize(int, char**) override { ++InitCount; }
		void Shutdown() override {}
		void Poll() { gPolls.fetch_add(1, std::memory_order_relaxed); }
	};

	// FC has no topo deps (leaf) — another root-level singleton
	struct FC : public TSingleton<FC>, public IPlugin<IInit, IShutdown>
	{
		static FC& Get() { static FC I; return I; }
		int InitCount = 0;
		void Initialize(int, char**) override { ++InitCount; }
		void Shutdown() override {}
		void Poll() { gPolls.fetch_add(1, std::memory_order_relaxed); }
	};

	using FSingletons = TTypeList<FA, FB, FC>;
	// level table via Topo: level 0 = {FA, FC}, level 1 = {FB}
	using FLevels = typename Topo::TLevels_t<FSingletons, FDefaultSlot>;
	static_assert(FLevels::Count == 2, "FA/FC are level 0, FB is level 1");
}

int main()
{
	FLayer<> Base;

	// Drive singletons level-by-level via FLayer::ForEach — the singleton branch
	// (every level type is a TSingleton) hands each T::Get() to the visitor.
	Base.ForEach<FLevels>([](auto& S) { S.Poll(); });
	if (gPolls.load() != 3)
	{
		std::printf("[FAIL] polled=%d want=3\n", gPolls.load());
		return 1;
	}

	// filter the table first (Core::Query::Select keeps matching types), then drive
	gPolls.store(0);
	using FSelected = typename decltype(TQuery<FSingletons>{}.Select<IPlugin<IInit, IShutdown>>())::FResult;
	using FSelectedLevels = typename Topo::TLevels_t<FSelected, FDefaultSlot>;
	Base.ForEach<FSelectedLevels>([](auto& S) { S.Poll(); });
	if (gPolls.load() != 3)
	{
		std::printf("[FAIL] filtered singletons polled=%d want=3\n", gPolls.load());
		return 1;
	}

	// drive the fixed lifecycle (Initialize/Shutdown)
	Base.ForEach<FSelectedLevels>([](auto& S) { S.Initialize(0, nullptr); });
	if (FA::Get().InitCount != 1 || FB::Get().InitCount != 1 || FC::Get().InitCount != 1)
	{
		std::puts("[FAIL] singletons not Initialize'd exactly once");
		return 1;
	}

	std::puts("ok: FLayer::ForEach singleton branch + Query.Select drove singletons");
	return 0;
}
