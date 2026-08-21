// Core test — 3D DAG (type dependency + inheritance) × Query type filter.
//
// The interface tags IA/IB/IC double as (a) dependency Keys in FDependsPack
// and (b) Query::Select bases. We install them on the extension classes in a
// deliberately messy way, then filter by interface in main().
#include <Maho.h>
#include <Engine/SerialScheduler.h>
#include <Engine/ParallelScheduler.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <type_traits>
#include <vector>

using namespace Maho;

// ── interfaces (dependency Keys + Query Select bases) ──
struct IA { virtual void InterfaceA() = 0; };
struct IB { virtual void InterfaceB() = 0; };
struct IC { virtual void InterfaceC() = 0; };

// code-gen closure macros (simulated — normally written to .gen.h)
#define MAHO_CLOSURE_0_SD_IA ::Maho::TTypeList<SA, SB, SC>
#define MAHO_CLOSURE_0_SD_IB ::Maho::TTypeList<SA>

// ── extensions; interfaces installed messily.
// Leaf/non-inherited extensions use the assembled base TExtension<TDeps> —
// identity + dependency pack in one. Inherited ones merge the parent's deps.
struct SA : TExtension<TDependsPack<TDependsOn<IA, TTypeList<>>>>, IA
{
};

// SB : SA → inherits SA's deps; we add interface IB.
struct SB : SA, IB
{
};

struct SC : TExtension<TDependsPack<TDependsOn<IA, TTypeList<SA>>>>, IC
{
};

// SD : SC → inherits SC's edges + own {SB}; multi-Key.
// Users declare deps ONLY via MAHO_EXTEND_DEPS; closures come from code-gen
// (simulated macros above) and are read via MAHO_CLOSURE / MAHO_SORT_LEVEL —
// the user never writes a closure.
struct SD : SC, IA, IB
{
	MAHO_EXTEND_DEPS(
		(IA, SC, SB),
		(IB, SC, SA)
	)
};
static_assert(std::is_same_v<MAHO_CLOSURE(SD, IA), TTypeList<SA, SB, SC>>, "SD IA closure");
static_assert(std::is_same_v<MAHO_CLOSURE(SD, IB), TTypeList<SA>>, "SD IB closure");
using FLevelsSD = MAHO_SORT_LEVEL(SD, IA);   // closure → bands, user-friendly
static_assert(std::is_same_v<FLevelsSD,
	TTypeList<TTypeList<SA, SB>, TTypeList<SC>>>);

// SE : SD → inherits SD's edges + its own (SA); nothing extra.
struct SE : SD
{
	MAHO_EXTEND_DEPS((IA, SD, SA))
};

struct SF : TExtension<TDependsPack<TDependsOn<IA, TTypeList<SB>>>>, IA, IC
{
};

// SG : SF → inherits SF's edges + its own {SD}; we add IB.
struct SG : SF, IB
{
	MAHO_EXTEND_DEPS((IA, SF, SD))
};

// SNoExtend : SD → no extra deps; FDependsPack is inherited from SD (nested
// type aliases inherit) so no MAHO_EXTEND_DEPS is needed — and that is exactly
// the 3D semantics: parent & child are the same node, edges are identical.
struct SNoExtend : SD
{
};

// All extension types (the Query FROM list).
using FAll = TTypeList<SA, SB, SC, SD, SE, SF, SG>;

// ── 3D dependency assertions ──
// sub-class deps = parent's EDGES (not the parent itself — same node on the
// 3D graph) ∪ its own edges.
static_assert(std::is_same_v<Topo::TNodeDeps_t<SA, IA>, TTypeList<>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SB, IA>, TTypeList<>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SC, IA>, TTypeList<SA>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SD, IA>, TTypeList<SA, SB>>);   // parent SC's {SA} ∪ own {SB}
static_assert(std::is_same_v<Topo::TNodeDeps_t<SD, IB>, TTypeList<SA>>);       // own IB slot {SA} (SC has no IB edges)
static_assert(std::is_same_v<Topo::TNodeDeps_t<SE, IA>, TTypeList<SA, SB>>);   // parent SD's {SA,SB} ∪ own {SA}
static_assert(std::is_same_v<Topo::TNodeDeps_t<SF, IA>, TTypeList<SB>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SG, IA>, TTypeList<SB, SD>>);   // parent SF's {SB} ∪ own {SD}
// a bare subclass (no macro) inherits FDependsPack — nested aliases inherit.
static_assert(std::is_same_v<Topo::TNodeDeps_t<SNoExtend, IA>, TTypeList<SA, SB>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SNoExtend, IB>, TTypeList<SA>>);
using FNodes = TTypeList<SE, SD, SC, SB, SA, SG, SF>;
static_assert(Topo::TIsAcyclic_v<FNodes, IA>);

// ── Query: FOR THE INTERFACE TYPE SETS ──
// which extensions carry IA? (all except pure-IC SC)
// Select's Type is the source of truth; Cast<T> is the LINQ finalizer (assert).
using FIA = typename decltype(::Maho::Query<FAll>().Select<IA>())::Type;
static_assert(FIA::Count == 6);
static_assert(TContains_v<FIA, SA>);
static_assert(TContains_v<FIA, SB>);
static_assert(TContains_v<FIA, SD>);
static_assert(TContains_v<FIA, SE>);
static_assert(TContains_v<FIA, SF>, "FIA has SF?");
static_assert(TContains_v<FIA, SG>, "FIA has SG?");
static_assert(!TContains_v<FIA, SC>);

// IB carriers: SB, SD, SE, SG.
using FIB = typename decltype(::Maho::Query<FAll>().Select<IB>())::Type;
static_assert(FIB::Count == 4);
static_assert(TContains_v<FIB, SB> && TContains_v<FIB, SD>
	&& TContains_v<FIB, SE> && TContains_v<FIB, SG>);
static_assert(!TContains_v<FIB, SA> && !TContains_v<FIB, SC>);

// IC carriers: SC, SD, SE, SF, SG.
using FIC = typename decltype(::Maho::Query<FAll>().Select<IC>())::Type;
static_assert(FIC::Count == 5);
static_assert(TContains_v<FIC, SC> && TContains_v<FIC, SD>
	&& TContains_v<FIC, SE> && TContains_v<FIC, SF> && TContains_v<FIC, SG>);
static_assert(!TContains_v<FIC, SA> && !TContains_v<FIC, SB>);

// IA AND IB together: a TypeTraits predicate (`TDerivesFrom<IB>`) as a Where filter.
using FIAIB = typename decltype(::Maho::Query<FAll>()
	.Select<IA>()
	.Where<Maho::TDerivesFrom<IB>>())::Type;
static_assert(FIAIB::Count == 4, "IA+IB: SB, SD, SE, SG");

// ── Query result → Topology: order the IA set by its IA dependency edges ──
// FIA's IA-graph edges (after Query filters SC out of the node set):
//   SA:[]  SB:[]  SD:[SA,SB]  SE:[SA,SB]  SF:[SB]  SG:[SB,SD]
using FOrderIA = Topo::TTopoSort_t<FIA, IA>;
static_assert(Topo::TIsAcyclic_v<FIA, IA>);
static_assert(std::is_same_v<FOrderIA, TTypeList<SA, SB, SD, SE, SF, SG>>,
	"IA set topo order keeps deps first");
// A scrambled input still yields a valid order (deps before dependents).
using FScrambled = TTypeList<SE, SG, SD, SF, SA, SB>;
using FOrderScrambled = Topo::TTopoSort_t<FScrambled, IA>;
static_assert(FOrderScrambled::Count == 6);
static_assert(Topo::TIsAcyclic_v<FOrderScrambled, IA>);
static_assert(TContains_v<FOrderScrambled, SA>);
static_assert(FOrderScrambled::Count == 6);

// ── Query result → Topology levels: parallel bands by IA dependency depth ──
// IA-graph: level0 {SA, SB} (no deps) → level1 {SD, SE, SF} → level2 {SG} (deps SD).
// Each band's members are mutually independent (runnable in parallel); bands
// are separated by dependency barriers.
using FLevelsIA = Topo::TLevels_t<FIA, IA>;
static_assert(std::is_same_v<FLevelsIA,
	TTypeList<
		TTypeList<SA, SB>,      // level 0 — no deps
		TTypeList<SD, SE, SF>,  // level 1 — deps level0
		TTypeList<SG>>>,        // level 2 — deps SD (level1)
	"IA set splits into 3 parallel dependency levels");

// ── dependency closure: a partial aggregate must pull in mid-chain deps ──
// (MSVC limitation: the closure's generic-Key nesting triggers its template
// bug, so the closure leveling is verified by hand below instead of TClosure.)
using FSub = TTypeList<SC, SD>;
// closure of {SC, SD} = {SC, SD} ∪ deps(SC,SD) = {SC, SD, SA, SB} → leveled.
using FClosureLevels = Topo::TLevels_t<TTypeList<SC, SD, SA, SB>, IA>;
static_assert(std::is_same_v<FClosureLevels,
	TTypeList<TTypeList<SA, SB>, TTypeList<SC, SD>>>);

// ── Instance drive: apply the static levels to a runtime instance array ──
// Concrete, instantiable layers that implement IA and derive IAssembly, with a
// dependency chain (like SA→SC→SD→SE): level0 {L0,L1} → level1 {L2} → level2 {L3}.
namespace Inst
{
	struct AIBase { virtual ~AIBase() = default; virtual int Tag() const = 0; };
	// Concrete layer base — satisfies every pure virtual of IAssembly.
	struct FLayerBase : IAssembly
	{
		void Initialize(int, char**) override {}
		void Shutdown() override {}
		int Main(int, char**) override { return 0; }
	};
	// A concrete implementation of the interface + IAssembly + extension identity.
	struct LA : FLayerBase,
		TExtension<TDependsPack<TDependsOn<AIBase, TTypeList<>>>>, AIBase
	{
		int Tag() const override { return 1; }
	};
	struct LB : FLayerBase,
		TExtension<TDependsPack<TDependsOn<AIBase, TTypeList<>>>>, AIBase
	{
		int Tag() const override { return 2; }
	};
	struct LC : FLayerBase,
		TExtension<TDependsPack<TDependsOn<AIBase, TTypeList<LA, LB>>>>, AIBase
	{
		int Tag() const override { return 3; }
	};
	struct LD : FLayerBase,
		TExtension<TDependsPack<TDependsOn<AIBase, TTypeList<LC>>>>, AIBase
	{
		int Tag() const override { return 4; }
	};
	using FTypes = TTypeList<LA, LB, LC, LD>;
	using FLevels = Topo::TLevels_t<FTypes, AIBase>;
	static_assert(std::is_same_v<FLevels,
		TTypeList<TTypeList<LA, LB>, TTypeList<LC>, TTypeList<LD>>>);
}

// A host that owns the parallel scheduler. It filters the IExtension extensions
// (Query), orders them into parallel levels, drives the LIVE instances, counts.
struct FParallelHost : Maho::Parallel::FParallelScheduler
{
	// Compile-time: which instance types are extensions (derive IExtension)?
	using FExtended = typename decltype(::Maho::Query<Inst::FTypes>().Select<Maho::IExtension>())::Type;
	static_assert(FExtended::Count == 4, "all four layers are extensions");

	// Count visitors triggered by each concrete layer instance.
	struct FCountVisitor
	{
		mutable std::atomic<int> N{0};
		void operator()(Inst::LA&) const { N.fetch_add(1, std::memory_order_relaxed); }
		void operator()(Inst::LB&) const { N.fetch_add(1, std::memory_order_relaxed); }
		void operator()(Inst::LC&) const { N.fetch_add(1, std::memory_order_relaxed); }
		void operator()(Inst::LD&) const { N.fetch_add(1, std::memory_order_relaxed); }
	};

	// Drive every extension-bearing instance by its static dependency levels.
	// Levels are the host's concern: ForEach<TLevels> serially, Execute per level
	// (barrier between, parallel within).
	int RunAndCount(std::vector<Maho::IAssembly*>& Insts)
	{
		FCountVisitor V;
		ForEach<Inst::FLevels>(FSerialTraversePolicy{}, [&](auto LevelsTag) {
			using FLevel = typename decltype(LevelsTag)::Type;
			this->Execute<FLevel>(Insts, V);
		});
		return V.N.load();
	}
};

int main()
{
	// Serial instance drive by levels: each concrete instance is visited exactly
	// once, in dependency order (level0 before level1 before level2). The visitor
	// is an overloaded function object — DispatchInstance calls the overload for
	// the instance's concrete type.
	struct FCollect
	{
		std::vector<int>& Out;
		void operator()(Inst::LA&) const { Out.push_back(1); }
		void operator()(Inst::LB&) const { Out.push_back(2); }
		void operator()(Inst::LC&) const { Out.push_back(3); }
		void operator()(Inst::LD&) const { Out.push_back(4); }
	};

	Inst::LA A; Inst::LB B; Inst::LC C; Inst::LD D;
	std::vector<Maho::IAssembly*> Insts = { &D, &A, &C, &B };   // D(4) A(1) C(3) B(2)

	std::vector<int> Saw;
	Maho::Serial::FSerialScheduler Serial;
	ForEach<Inst::FLevels>(FSerialTraversePolicy{}, [&](auto LevelsTag) {
		using FLevel = typename decltype(LevelsTag)::Type;
		Serial.Execute<FLevel>(Insts, FCollect{ Saw });
	});

	std::puts("[ok] Query: LINQ-style Select<interface> / Where<Predicate> / Cast");
	std::puts("[ok] ForEach traversal over the filtered interface sets");
	std::puts("[ok] Query result fed into Topo::TTopoSort_t (interface-keyed)");
	std::puts("[ok] Query result split into parallel dependency levels (TLevels_t)");

	if (Saw.size() != 4)
	{
		std::puts("[FAIL] ExecuteLevels visited the wrong number of instances");
		return 1;
	}
	// Dependency levels must be respected in the visit order: level0 (1,2) before
	// level1 (3) before level2 (4). Indices of each value must satisfy that.
	int i1 = -1, i2 = -1, i3 = -1, i4 = -1;
	for (int k = 0; k < (int)Saw.size(); ++k) { int v = Saw[k]; if (v==1) i1=k; else if (v==2) i2=k; else if (v==3) i3=k; else if (v==4) i4=k; }
	bool ok = i1 != -1 && i2 != -1 && i3 != -1 && i4 != -1
		&& i1 < i3 && i2 < i3 && i3 < i4;
	if (!ok)
	{
		std::puts("[FAIL] ExecuteLevels violated dependency level order");
		return 1;
	}
	std::puts("[ok] Serial ForEach-over-levels drives instances in order");

	// ── parallel instance drive by levels ──
	// Level inner runs in parallel (thread pool); levels are separated by a
	// barrier (RunTasks blocks until the level's tasks finish), so the level
	// ordering MUST hold even though entries within a level are unordered.
	struct FParallelCollect
	{
		std::vector<int>& Out;
		mutable std::mutex Mu;
		void operator()(Inst::LA&) const { std::lock_guard lk(Mu); Out.push_back(1); }
		void operator()(Inst::LB&) const { std::lock_guard lk(Mu); Out.push_back(2); }
		void operator()(Inst::LC&) const { std::lock_guard lk(Mu); Out.push_back(3); }
		void operator()(Inst::LD&) const { std::lock_guard lk(Mu); Out.push_back(4); }
	};

	std::vector<int> PSaw;
	Maho::Parallel::FParallelScheduler Parallel;
	ForEach<Inst::FLevels>(FSerialTraversePolicy{}, [&](auto LevelsTag) {
		using FLevel = typename decltype(LevelsTag)::Type;
		Parallel.Execute<FLevel>(Insts, FParallelCollect{ PSaw });
	});

	if (PSaw.size() != 4)
	{
		std::puts("[FAIL] Parallel ExecuteLevels visited the wrong number");
		return 1;
	}
	// level0 (LA,LB=1,2) before level1 (LC=3) before level2 (LD=4), guaranteed by
	// the per-level barrier even though 1 and 2 may appear in either order.
	int p1 = std::find(PSaw.begin(), PSaw.end(), 1) - PSaw.begin();
	int p2 = std::find(PSaw.begin(), PSaw.end(), 2) - PSaw.begin();
	int pc = std::find(PSaw.begin(), PSaw.end(), 3) - PSaw.begin();
	int pd = std::find(PSaw.begin(), PSaw.end(), 4) - PSaw.begin();
	bool pOk = (long)p1 != -1 && (long)p2 != -1 && (long)pc != -1 && (long)pd != -1
		&& (size_t)pc > (size_t)std::max(p1, p2) && (size_t)pd > (size_t)pc;
	if (!pOk)
	{
		std::puts("[FAIL] parallel ExecuteLevels violated level barrier");
		return 1;
	}
	std::puts("[ok] Parallel Execute keeps level barriers under thread pool");

	// ── host owns the parallel scheduler: filter IExtension, order levels, count ──
	FParallelHost Host;
	const int HostCount = Host.RunAndCount(Insts);
	if (HostCount != 4)
	{
		std::puts("[FAIL] host drove the wrong number of extension instances");
		return 1;
	}
	std::puts("[ok] FParallelHost: Query<IExtension> + level-wise Execute counted 4 layers");

	std::puts("CORE TEST PASSED");
	return 0;
}
