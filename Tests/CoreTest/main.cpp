// Core test — 3D DAG: dependency graph (x/y) × inheritance (z).
//
// Each extension declares its FULL dependency pack explicitly:
//   using FDependsPack = TCatch<Parent::FDependsPack, TDependsPack<own slots>>::Type;
// so a derived type's edges = parent's edges (recursively) ∪ its own.
// The Key (IA/IB/...) is a TYPE — an interface tag, matched per-slot.
#include <Maho.h>

#include <cstdio>
#include <type_traits>

using namespace Maho;

// ── interface tags (the dependency Keys, also usable as Query Select bases) ──
struct IA { virtual ~IA() = default; };
struct IB { virtual ~IB() = default; };
struct IC { virtual ~IC() = default; };

// ── extensions ──
struct SA : IExtension
{
	using FDependsPack = TDependsPack<TDependsOn<IA, TTypeList<>>>;
};

// SB inherits SA: its pack = SA's pack (no own edges).
struct SB : SA
{
	using FDependsPack = SA::FDependsPack;
};

struct SC : IExtension
{
	using FDependsPack = TDependsPack<TDependsOn<IA, TTypeList<SA>>>;
};

// SD : SC + own deps — pack = SC's [SA] ∪ own [SB, SC] (same KEY IA spliced).
struct SD : SC
{
	using FDependsPack = Topo::TConcatPacks_t<SC::FDependsPack,
		TDependsPack<TDependsOn<IA, TTypeList<SB, SC>>>>;
};

// SE : SD + own deps — pack = SD's ∪ own [SD, SA].
struct SE : SD
{
	using FDependsPack = Topo::TConcatPacks_t<SD::FDependsPack,
		TDependsPack<TDependsOn<IA, TTypeList<SD, SA>>>>;
};

struct SF : IExtension
{
	using FDependsPack = TDependsPack<TDependsOn<IA, TTypeList<SB>>>;
};

// SG : SF + own deps — pack = SF's [SB] ∪ own [SD].
struct SG : SF
{
	using FDependsPack = Topo::TConcatPacks_t<SF::FDependsPack,
		TDependsPack<TDependsOn<IA, TTypeList<SD>>>>;
};

// ── 3D assertions: per-key deps along the inheritance chain ──
static_assert(std::is_same_v<Topo::TNodeDeps_t<SA, IA>, TTypeList<>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SB, IA>, TTypeList<>>);          // SB == SA (z-axis)
static_assert(std::is_same_v<Topo::TNodeDeps_t<SC, IA>, TTypeList<SA>>);
// SD : SC[SA] ∪ own [SB,SC] — edges are spliced (order: SC's then own).
static_assert(std::is_same_v<Topo::TNodeDeps_t<SD, IA>,
	TTypeList<SA, SB, SC>>);
// SE : SD(SA,SB,SC) ∪ own [SD,SA] — SA appears again; dedup is handled by the
// query/filter layer (a later concern), not by pack concat.
static_assert(std::is_same_v<Topo::TNodeDeps_t<SE, IA>,
	TTypeList<SA, SB, SC, SD, SA>>);
static_assert(std::is_same_v<Topo::TNodeDeps_t<SF, IA>, TTypeList<SB>>);
// SG : SF[SB] ∪ own [SD].
static_assert(std::is_same_v<Topo::TNodeDeps_t<SG, IA>, TTypeList<SB, SD>>);

// full-node topo sort at IA (all nodes)
using FNodes = TTypeList<SE, SD, SC, SB, SA, SG, SF>;
static_assert(Topo::TIsAcyclic_v<FNodes, IA>);
using FOrder = Topo::TTopoSort_t<FNodes, IA>;
static_assert(FOrder::Count == 7);

int main()
{
	std::puts("[ok] 3D DAG: per-key deps merge along inheritance chain");
	std::puts("CORE TEST PASSED");
	return 0;
}
