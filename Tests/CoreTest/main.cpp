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
// ② Topology: static dependency ordering (topo sort + parallel levels).
// ───────────────────────────────────────────────────────────────────────
namespace Tp
{
	// A graph keyed by two independent phases.
	enum class EPhase { Init, Tick };

	// ── Init graph: FBase → FInput → FSystem; FOther is data-only. ──
	struct FBase
	{
		using FDependsPack = TDependsPack<
			TDependsOn<EPhase::Init, TTypeList<>>,
			TDependsOn<EPhase::Tick, TTypeList<>>>;
	};
	struct FInput
	{
		using FDependsPack = TDependsPack<TDependsOn<EPhase::Init, TTypeList<FBase>>>;
	};
	struct FSystem
	{
		using FDependsPack = TDependsPack<
			TDependsOn<EPhase::Init, TTypeList<FBase, FInput>>,
			TDependsOn<EPhase::Tick, TTypeList<FBase>>>;
	};
	struct FOther {};   // declares no pack → zero deps, resolves to empty.

	using FNodes = TTypeList<FSystem, FInput, FBase, FOther>;
	static_assert(FNodes::Count == 4);

	//── per-key deps resolution ──
	static_assert(std::is_same_v<Topo::TNodeDeps_t<FSystem, EPhase::Init>, TTypeList<FBase, FInput>>);
	static_assert(std::is_same_v<Topo::TNodeDeps_t<FSystem, EPhase::Tick>, TTypeList<FBase>>);
	static_assert(std::is_same_v<Topo::TNodeDeps_t<FBase,  EPhase::Init>, TTypeList<>>);
	static_assert(std::is_same_v<Topo::TNodeDeps_t<FOther, EPhase::Init>, TTypeList<>>, "no pack → empty deps");

	//── cycle checks ──
	static_assert(Topo::TIsAcyclic_v<FNodes, EPhase::Init>);
	static_assert(Topo::TIsAcyclic_v<FNodes, EPhase::Tick>);

	//── topological order at Init: deps before dependents, others at the end
	//   (post-order over the node list preserves declaration order among peers) ──
	using FOrderInit = Topo::TTopoSort_t<FNodes, EPhase::Init>;
	static_assert(std::is_same_v<TTypeList<FBase, FInput, FSystem, FOther>, FOrderInit>,
		"Init order = Base, Input, System, Other");

	using FOrderTick = Topo::TTopoSort_t<FNodes, EPhase::Tick>;
	// Tick graph: FSystem→FBase; FInput/FOther independent. Post-order visit of
	// (FSystem,FInput,FBase,FOther) yields FBase,FSystem,FInput,FOther.
	static_assert(std::is_same_v<TTypeList<FBase, FSystem, FInput, FOther>, FOrderTick>);

	//── parallel levels at Init:
	//   level0 = {FBase, FOther} (independent), level1={FInput}, level2={FSystem} ──
	using FLevelsInit = Topo::TLevels_t<FNodes, EPhase::Init>;
	static_assert(std::is_same_v<
		TTypeList<
			TTypeList<FBase, FOther>,
			TTypeList<FInput>,
			TTypeList<FSystem>>,
		FLevelsInit>);

	//── at Tick: level0={FInput, FBase, FOther} (node declaration order),
	//   level1={FSystem} ──
	using FLevelsTick = Topo::TLevels_t<FNodes, EPhase::Tick>;
	static_assert(std::is_same_v<
		TTypeList<
			TTypeList<FInput, FBase, FOther>,
			TTypeList<FSystem>>,
		FLevelsTick>);

	//── reverse dependency: who depends on FBase at Init? ──
	using FDependentsOfBase = Topo::TFindDependents_t<FNodes, EPhase::Init, FBase>;
	static_assert(std::is_same_v<TTypeList<FSystem, FInput>, FDependentsOfBase>);

	//── cycles ──
	struct FZ;
	struct FY { using FDependsPack = TDependsPack<TDependsOn<EPhase::Init, TTypeList<FZ>>>; };
	struct FZ { using FDependsPack = TDependsPack<TDependsOn<EPhase::Init, TTypeList<FY>>>; };
	using FCycle = TTypeList<FY, FZ>;
	static_assert(Topo::THasCycle_v<FCycle, EPhase::Init>, "mutual FY/FZ is a cycle");

	struct FSelf { using FDependsPack = TDependsPack<TDependsOn<EPhase::Init, TTypeList<FSelf>>>; };
	static_assert(Topo::THasCycle_v<TTypeList<FSelf>, EPhase::Init>, "self-loop is a cycle");

	//── default slot (no stage): plugin wants plain ordering ──
	struct FD1 { using FDependsPack = TDependsPack<TDependsOn<FDefaultSlot, TTypeList<>>>; };
	struct FD2 { using FDependsPack = TDependsPack<TDependsOn<FDefaultSlot, TTypeList<FD1>>>; };
	using FDefNodes = TTypeList<FD2, FD1>;
	using FDefOrder = Topo::TTopoSort_t<FDefNodes, FDefaultSlot>;
	static_assert(std::is_same_v<TTypeList<FD1, FD2>, FDefOrder>);

	//── MyExtension: inherits the dependency table AND the per-stage slots; the
	//   inherited FDependsPack must carry the slots (regression for the alias bug).
	struct FX { using FDependsPack = TDependsPack<TDependsOn<EPhase::Init, TTypeList<>>>; };
	struct FY2 { using FDependsPack = TDependsPack<TDependsOn<EPhase::Init, TTypeList<FX>>>; };
	struct FMyExtension
		: TExtension<FX, FY2>
		, TDependsPack<TDependsOn<EPhase::Init, TTypeList<FX, FY2>>>
	{
	};
	using FMyNodes = TTypeList<FMyExtension, FY2, FX>;
	using FMyDeps = Topo::TNodeDeps_t<FMyExtension, EPhase::Init>;
	static_assert(std::is_same_v<FMyDeps, TTypeList<FX, FY2>>,
		"inherited TDependsPack slot must carry TTypeList<FX,FY2>");
	static_assert(Topo::TIsAcyclic_v<FMyNodes, EPhase::Init>);
	using FMyOrder = Topo::TTopoSort_t<FMyNodes, EPhase::Init>;
	static_assert(FMyOrder::Count == 3);
	static_assert(Topo::TIsAcyclic_v<FMyOrder, EPhase::Init>);

	void Run()
	{
		std::puts("[ok] Topology: static dependency ordering (topo sort + levels + cycles)");
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
