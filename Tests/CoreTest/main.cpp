// Core test — 3D DAG (type dependency + inheritance) × Query type filter.
//
// The interface tags IA/IB/IC double as (a) dependency Keys in FDependsPack
// and (b) Query::Select bases. We install them on the extension classes in a
// deliberately messy way, then filter by interface in main().
#include <Maho.h>

#include <cstdio>
#include <type_traits>

using namespace Maho;

// ── interfaces (dependency Keys + Query Select bases) ──
struct IA { virtual void InterfaceA() = 0; };
struct IB { virtual void InterfaceB() = 0; };
struct IC { virtual void InterfaceC() = 0; };

// ── extensions; interfaces installed messily ──
struct SA : IExtension, IA
{
	using FDependsPack = TDependsPack<TDependsOn<IA, TTypeList<>>>;
};

// SB : SA → carries SA's IA, we also add IB.
struct SB : SA, IB
{
	using FDependsPack = SA::FDependsPack;
};

struct SC : IExtension, IC
{
	using FDependsPack = TDependsPack<TDependsOn<IA, TTypeList<SA>>>;
};

// SD : SC → carries SC's IC; we add IA + IB.
struct SD : SC, IA, IB
{
	using FDependsPack = Topo::TConcatPacks_t<SC::FDependsPack,
		TDependsPack<TDependsOn<IA, TTypeList<SB, SC>>>>;
};

// SE : SD → carries IA,IB,IC; nothing extra.
struct SE : SD
{
	using FDependsPack = Topo::TConcatPacks_t<SD::FDependsPack,
		TDependsPack<TDependsOn<IA, TTypeList<SD, SA>>>>;
};

struct SF : IExtension, IA, IC
{
	using FDependsPack = TDependsPack<TDependsOn<IA, TTypeList<SB>>>;
};

// SG : SF → carries IA,IC; we add IB.
struct SG : SF, IB
{
	using FDependsPack = Topo::TConcatPacks_t<SF::FDependsPack,
		TDependsPack<TDependsOn<IA, TTypeList<SD>>>>;
};

// All extension types (the Query FROM list).
using FAll = TTypeList<SA, SB, SC, SD, SE, SF, SG>;

// ── 3D dependency assertions (unchanged from the inheritance model) ──
static_assert(std::is_same_v<Topo::TNodeDeps_t<SA, IA>, TTypeList<>>);
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
// NOTE: Select's Type is the source of truth; As<B> is a cast/assert helper
// (its auto-returned decltype is unreliable on MSVC for these tags).
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

int main()
{
	// Traverse each filtered interface set with ForEach(serial policy).
	int Count = 0;
	ForEach<FIA>(FSerialTraversePolicy{}, [&](auto Tag) {
		(void)Tag;
		++Count;
	});
	ForEach<FIB>(FSerialTraversePolicy{}, [&](auto Tag) {
		(void)Tag;
		++Count;
	});
	ForEach<FIC>(FSerialTraversePolicy{}, [&](auto Tag) {
		(void)Tag;
		++Count;
	});
	if (Count != 6 + 4 + 5)
	{
		std::puts("[FAIL] interface traversal count mismatch");
		return 1;
	}
	std::puts("[ok] Query: Select<interface> + Where<TAnd<TDerivesFrom<IB>>>");
	std::puts("[ok] ForEach traversal over the filtered interface sets");
	std::puts("CORE TEST PASSED");
	return 0;
}
