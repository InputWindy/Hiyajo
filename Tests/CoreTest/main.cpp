// Core smoke test — exercises the engine core (TypeList / Query / Topology /
// Delegate) directly, with no plugin/extension/codegen machinery.
#include <Maho.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <type_traits>

using namespace Maho;

// ───────────────────────────────────────────────────────────────────────
// ① TypeList + Query: compile-time type filtering.
// ───────────────────────────────────────────────────────────────────────
namespace Q
{
	struct IRenderFeature { virtual ~IRenderFeature() = default; };
	struct FTickableTag    { virtual ~FTickableTag() = default; };

	struct ACapture  : IRenderFeature, FTickableTag { };
	struct AForward  : IRenderFeature, FTickableTag { };
	struct AHeadless : IRenderFeature { };
	struct AGameplay : FTickableTag { };
	struct AStreamer { };

	using FList = TTypeList<ACapture, AForward, AHeadless, AGameplay, AStreamer>;
	static_assert(FList::Count == 5);

	// Chained value-cascade: each .With/.Not returns a new query value.
	// render features: Capture + Forward + Headless.
	constexpr auto QRender = TQuery<FList>{}.With<IRenderFeature>();
	using FRender = decltype(QRender)::Type;
	static_assert(FRender::Count == 3);
	static_assert(TContains_v<FRender, ACapture>);
	static_assert(!TContains_v<FRender, AGameplay>);

	// render AND tickable: Capture + Forward.
	constexpr auto QRenderTick = QRender.With<FTickableTag>();
	using FRenderTick = decltype(QRenderTick)::Type;
	static_assert(FRenderTick::Count == 2);
	static_assert(TContains_v<FRenderTick, ACapture>);

	// tickable but NOT render: only Gameplay.
	constexpr auto QTickNotRender = TQuery<FList>{}.With<FTickableTag>().Not<IRenderFeature>();
	using FTickNotRender = decltype(QTickNotRender)::Type;
	static_assert(FTickNotRender::Count == 1);
	static_assert(TContains_v<FTickNotRender, AGameplay>);

	// With then Not of the same base → empty.
	constexpr auto QCancel = TQuery<FList>{}.With<IRenderFeature>().Not<IRenderFeature>();
	using FCancel = decltype(QCancel)::Type;
	static_assert(FCancel::Count == 0);

	void Run()
	{
		std::puts("[ok] Query: With/Not type filtering");
	}
}

// ───────────────────────────────────────────────────────────────────────
// ② Topology: cycle detection, topological order, dependency levels.
// ───────────────────────────────────────────────────────────────────────
namespace Tp
{
	// Fake "phase" key shared by the graph.
	enum class EPhase { Main };

	// Three nodes with a dependency chain, plus a parallel root.
	struct FBase   { using FDependsPack = TDependsPack<TDependsOn<EPhase::Main, TTypeList<>>>; };
	struct FInput  { using FDependsPack = TDependsPack<TDependsOn<EPhase::Main, TTypeList<FBase>>>; };
	struct FSystem { using FDependsPack = TDependsPack<TDependsOn<EPhase::Main, TTypeList<FBase, FInput>>>; };

	using FNodes = TTypeList<FSystem, FInput, FBase>;

	static_assert(Topo::TIsAcyclic_v<FNodes, EPhase::Main>);

	// Topological order: deps first.
	using FOrder = Topo::TTopoSort_t<FNodes, EPhase::Main>;
	static_assert(std::is_same_v<TTypeList<FBase, FInput, FSystem>, FOrder>);

	// Levels: 3 bands (FBase → FInput → FSystem).
	using FLevels = Topo::TLevels_t<FNodes, EPhase::Main>;
	static_assert(FLevels::Count == 3);

	// A cycle is detected (forward-declare to let FY/FZ mutually reference).
	struct FZ;
	struct FY { using FDependsPack = TDependsPack<TDependsOn<EPhase::Main, TTypeList<FZ>>>; };
	struct FZ { using FDependsPack = TDependsPack<TDependsOn<EPhase::Main, TTypeList<FY>>>; };
	using FCycle  = TTypeList<FY, FZ>;
	static_assert(std::is_same_v<Topo::TNodeDeps_t<FY, EPhase::Main>, TTypeList<FZ>>, "FY deps = FZ");
	static_assert(std::is_same_v<Topo::TNodeDeps_t<FZ, EPhase::Main>, TTypeList<FY>>, "FZ deps = FY");
	static_assert(Topo::THasCycle_v<FCycle, EPhase::Main>, "mutual FY/FZ is a cycle");
	static_assert(!Topo::TIsAcyclic_v<FCycle, EPhase::Main>);

	// a self-loop is also a cycle.
	struct FSelf { using FDependsPack = TDependsPack<TDependsOn<EPhase::Main, TTypeList<FSelf>>>; };
	static_assert(!Topo::TIsAcyclic_v<TTypeList<FSelf>, EPhase::Main>, "self loop is a cycle");

	// Reverse dependency lookup: who depends on FBase? (in FNodes declaration order)
	using FDependentsOfBase = Topo::TFindDependents_t<FNodes, EPhase::Main, FBase>;
	static_assert(std::is_same_v<TTypeList<FSystem, FInput>, FDependentsOfBase>);

	void Run()
	{
		std::puts("[ok] Topology: acyclic order + levels + reverse deps");
	}
}

// ───────────────────────────────────────────────────────────────────────
// ③ Delegate: multi-cast.
// ───────────────────────────────────────────────────────────────────────
namespace D
{
	void Run()
	{
		Maho::TMulticastDelegate<void(int)> OnHit;
		int Total = 0;
		OnHit.Add([&](int V) { Total += V; });
		OnHit.Add([&](int V) { Total += V * 10; });
		OnHit.Broadcast(3);
		assert(Total == 33);   // 3 + 30
		std::puts("[ok] Delegate: multicast broadcast");
	}
}

// ───────────────────────────────────────────────────────────────────────
// main
// ───────────────────────────────────────────────────────────────────────
int main()
{
	Q::Run();
	Tp::Run();
	D::Run();
	std::puts("CORE TEST PASSED");
	return 0;
}
