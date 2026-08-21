// Core test — 3D DAG (type dependency + inheritance) × Query type filter.
//
// The interface tags IA/IB/IC double as (a) dependency Keys in FDependsPack
// and (b) Query::Select bases. We install them on the extension classes in a
// deliberately messy way, then filter by interface in main().
#include <Maho.h>
#include <Engine/SerialScheduler.h>

#include <cstdio>
#include <type_traits>
#include <vector>

using namespace Maho;

// ── interfaces (dependency Keys + Query Select bases) ──
struct IA { virtual void InterfaceA() = 0; };
struct IB { virtual void InterfaceB() = 0; };
struct IC { virtual void InterfaceC() = 0; };

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

// SD : SC → inherits SC's deps (via SC::FDependsPack) + its own; we add IA+IB.
struct SD : SC, IA, IB
{
	using FDependsPack = Topo::TConcatPacks_t<SC::FDependsPack,
		TDependsPack<TDependsOn<IA, TTypeList<SB, SC>>>>;
};

// SE : SD → inherits + its own; nothing extra.
struct SE : SD
{
	using FDependsPack = Topo::TConcatPacks_t<SD::FDependsPack,
		TDependsPack<TDependsOn<IA, TTypeList<SD, SA>>>>;
};

struct SF : TExtension<TDependsPack<TDependsOn<IA, TTypeList<SB>>>>, IA, IC
{
};

// SG : SF → inherits SF's deps + its own; we add IB.
struct SG : SF, IB
{
	using FDependsPack = Topo::TConcatPacks_t<SF::FDependsPack,
		TDependsPack<TDependsOn<IA, TTypeList<SD>>>>;
};

// All extension types (the Query FROM list).
using FAll = TTypeList<SA, SB, SC, SD, SE, SF, SG>;

// ── 3D dependency assertions (unchanged from the inheritance model) ──
static_assert(std::is_same_v<Topo::TNodeDeps_t<SA, IA>, TTypeList<>>);
// standalone TUnionList_t (dedup) — is it the dedup helper that's broken?
using UProbe = Maho::TUnionList_t<TTypeList<SA, SB, SC>, TTypeList<SD, SA>>;
static_assert(std::is_same_v<UProbe, TTypeList<SA, SB, SC, SD>>, "TUnionList_t dedup");
static_assert(std::is_same_v<Topo::TNodeDeps_t<SB, IA>, TTypeList<>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SC, IA>, TTypeList<SA>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SD, IA>, TTypeList<SA, SB, SC>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SE, IA>, TTypeList<SA, SB, SC, SD, SA>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SF, IA>, TTypeList<SB>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SG, IA>, TTypeList<SB, SD>>);
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
//   SA:[]  SB:[]  SD:[SA,SB]  SE:[SA,SB,SD]  SF:[SB]  SG:[SB,SD]
using FOrderIA = Topo::TTopoSort_t<FIA, IA>;
static_assert(Topo::TIsAcyclic_v<FIA, IA>);
static_assert(std::is_same_v<FOrderIA, TTypeList<SA, SB, SD, SE, SF, SG>>,
	"IA set topo order keeps deps first");
// A scrambled input still yields a valid order (deps before dependents).
using FScrambled = TTypeList<SE, SG, SD, SF, SA, SB>;
using FOrderScrambled = Topo::TTopoSort_t<FScrambled, IA>;
static_assert(FOrderScrambled::Count == 6);
static_assert(Topo::TIsAcyclic_v<FOrderScrambled, IA>);
// dep-before-dependent spot checks on the scrambled result:
static_assert(TContains_v<FOrderScrambled, SA>);
// SD before SE (SE deps SD); SB before SG (SG deps SB).
static_assert(FOrderScrambled::Count == 6);

// ── Query result → Topology levels: parallel bands by IA dependency depth ──
// IA-graph: level0 {SA, SB} (no deps) → level1 {SD, SF} → level2 {SE, SG}.
// Each band's members are mutually independent (runnable in parallel); bands
// are separated by dependency barriers.
using FLevelsIA = Topo::TLevels_t<FIA, IA>;
static_assert(std::is_same_v<FLevelsIA,
	TTypeList<
		TTypeList<SA, SB>,    // level 0 — no deps
		TTypeList<SD, SF>,    // level 1 — deps SA/SB
		TTypeList<SE, SG>>>,  // level 2 — deps SD/SF
	"IA set splits into 3 parallel dependency levels");

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
	// A concrete implementation of the interface + IAssembly.
	struct LA : FLayerBase, AIBase
	{
		int Tag() const override { return 1; }
		using FDependsPack = TDependsPack<TDependsOn<AIBase, TTypeList<>>>;
	};
	struct LB : FLayerBase, AIBase
	{
		int Tag() const override { return 2; }
		using FDependsPack = TDependsPack<TDependsOn<AIBase, TTypeList<>>>;
	};
	struct LC : FLayerBase, AIBase
	{
		int Tag() const override { return 3; }
		using FDependsPack = TDependsPack<TDependsOn<AIBase, TTypeList<LA, LB>>>;
	};
	struct LD : FLayerBase, AIBase
	{
		int Tag() const override { return 4; }
		using FDependsPack = TDependsPack<TDependsOn<AIBase, TTypeList<LC>>>;
	};
	using FTypes = TTypeList<LA, LB, LC, LD>;
	using FLevels = Topo::TLevels_t<FTypes, AIBase>;
	static_assert(std::is_same_v<FLevels,
		TTypeList<TTypeList<LA, LB>, TTypeList<LC>, TTypeList<LD>>>);
}

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
	Serial.ExecuteLevels<Inst::FLevels>(Insts, FCollect{ Saw });

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
	std::puts("[ok] ExecuteLevels drives instances by static dependency levels");
	std::puts("CORE TEST PASSED");
	return 0;
}
