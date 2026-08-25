#pragma once

#include <Core/TypeList.h>

#include <cstddef>
#include <type_traits>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Public surface (forward declarations).
//
// A "Key" is any comparable constant passed as an `auto` non-type template
// parameter (an enum value, an integer, whatever the host domain uses to
// partition its phases). This header only provides per-key topological sort,
// leveling (parallel groups) and compile-time cycle detection; it does not
// know or care what a key means, nor does it encode forward/reverse or
// dependency strength — those are the host's policy.
//
// ── Integration example ────────────────────────────────────────────────
//   struct FMyBase;
//   struct FMyInput;
//   struct FMySystem;
//
//   struct FMySystem
//   {
//       using FDependsPack = TDependsPack<
//           TDependsOn<EEngineStage::Init, TTypeList<FMyBase, FMyInput>>>;
//   };
//
//   using FNodes = TTypeList<FMySystem, FMyBase, FMyInput>;
//
//   static_assert(Topo::TIsAcyclic_v<FNodes, EEngineStage::Init>);
//   using FLevels = Topo::TLevels_t<FNodes, EEngineStage::Init>;
//   // FLevels == TTypeList<
//   //     TTypeList<FMyBase, FMyInput>,   (level 0, may run in parallel)
//   //     TTypeList<FMySystem>>           (level 1)
// ───────────────────────────────────────────────────────────────────────

// ───────────────────────────────────────────────────────────────────────
// Declaration types.
// ───────────────────────────────────────────────────────────────────────

/**
 * Default dependency slot — the type key used when the drive is by lambda (no
 * stage). Classes that only need ordering use FDepends
 * (TTypeList<FDefaultSlot, TTypeList<...>>) keyed on this.
 */
struct FDefaultSlot {};

// ───────────────────────────────────────────────────────────────────────
// Topo namespace: the compile-time algorithms.
// ───────────────────────────────────────────────────────────────────────
namespace Topo
{

/**
 * A node's dependency list at a Key, read from its optional FDepends:
 *   using FDepends = TTypeList<Key, TTypeList<A, B>>;   // deps A, B at Key
 * Absent FDepends (or no slot for Key) → empty list. The FULL declaration is
 * used — NOT clipped to the scheduling set (deps may name out-of-set classes).
 */
template <typename TNode, typename Key, typename = void>
struct TNodeDepList
{
	using Type = TTypeList<>;
};

template <typename TNode, typename Key>
struct TNodeDepList<TNode, Key, std::void_t<typename TNode::FDepends>>
{
private:
	using FDepends = typename TNode::FDepends;

	// FDepends = TTypeList<DepKey, TTypeList<Deps...>>
	template <typename TKey, typename, typename>
	struct TDepsFor;
	template <typename TKey, typename DepKey>
	struct TDepsFor<TKey, TTypeList<DepKey, TTypeList<>>, std::void_t<>>
	{
		using Type = std::conditional_t<std::is_same_v<TKey, DepKey>,
			TTypeList<>, TTypeList<>>;
	};
	template <typename TKey, typename DepKey, typename... Deps>
	struct TDepsFor<TKey, TTypeList<DepKey, TTypeList<Deps...>>, std::void_t<>>
	{
		using Type = std::conditional_t<std::is_same_v<TKey, DepKey>,
			TTypeList<Deps...>, TTypeList<>>;
	};

public:
	using Type = typename TDepsFor<Key, FDepends, void>::Type;
};

// ── ① Cycle detection (DFS path coloring; short-circuits on back edge). ─

template <typename TNodes, typename Key, typename TNode, typename TPath>
struct TDfsCycle;

template <typename TNodes, typename Key, typename TPath, typename TDeps>
struct TDfsCycleList;

template <typename TNodes, typename Key, typename TPath>
struct TDfsCycleList<TNodes, Key, TPath, TTypeList<>> : std::false_type
{
};

template <typename TNodes, typename Key, typename TPath, typename THead, typename... TRest>
struct TDfsCycleList<TNodes, Key, TPath, TTypeList<THead, TRest...>>
	: std::bool_constant<
		TDfsCycle<TNodes, Key, THead, TPath>::value
		|| TDfsCycleList<TNodes, Key, TPath, TTypeList<TRest...>>::value>
{
};

template <bool bInPath, typename TNodes, typename Key, typename TNode, typename TPath>
struct TDfsCycleImpl;

template <typename TNodes, typename Key, typename TNode, typename TPath>
struct TDfsCycleImpl<true, TNodes, Key, TNode, TPath> : std::true_type
{
};

template <typename TNodes, typename Key, typename TNode, typename TPath>
struct TDfsCycleImpl<false, TNodes, Key, TNode, TPath>
	: std::bool_constant<
		TDfsCycleList<
			TNodes,
			Key,
			typename TCons<TNode, TPath>::Type,
			typename TNodeDepList<TNode, Key>::Type>::value>
{
};

template <typename TNodes, typename Key, typename TNode, typename TPath>
struct TDfsCycle
{
	static constexpr bool bInPath = TContains_v<TPath, TNode>;
	static constexpr bool value = TDfsCycleImpl<bInPath, TNodes, Key, TNode, TPath>::value;
};

template <typename TNodes, typename Key>
struct THasCycleFromAny;

template <typename Key>
struct THasCycleFromAny<TTypeList<>, Key> : std::false_type
{
};

template <typename Key, typename THead, typename... TRest>
struct THasCycleFromAny<TTypeList<THead, TRest...>, Key>
	: std::bool_constant<
		TDfsCycle<TTypeList<THead, TRest...>, Key, THead, TTypeList<>>::value
		|| THasCycleFromAny<TTypeList<TRest...>, Key>::value>
{
};

template <typename TNodes, typename Key>
inline constexpr bool THasCycle_v = THasCycleFromAny<TNodes, Key>::value;

template <typename TNodes, typename Key>
inline constexpr bool TIsAcyclic_v = !THasCycle_v<TNodes, Key>;

// ── ② Levels (parallel groups by dependency level). ─────────────────────

/**
 * Level of a node: 1 + max(level of deps); roots are 0.
 * Self-recursive: folds over the dep list, recursing into each dep's level.
 * Deps are read from FDepends (full declaration), not clipped to TNodes.
 */
template <typename TNodes, typename Key, typename TNode>
struct TNodeLevel
{
private:
	template <typename TDeps, int Acc>
	struct FMaxLevel
	{
		static constexpr int Value = Acc;
	};

	template <typename THead, typename... TRest, int Acc>
	struct FMaxLevel<TTypeList<THead, TRest...>, Acc>
	{
		static constexpr int HeadLevel = TNodeLevel<TNodes, Key, THead>::Value;
		static constexpr int NextAcc = (HeadLevel > Acc) ? HeadLevel : Acc;
		static constexpr int Value = FMaxLevel<TTypeList<TRest...>, NextAcc>::Value;
	};

public:
	static constexpr int Value =
		1 + FMaxLevel<typename TNodeDepList<TNode, Key>::Type, -1>::Value;
};

/** Max level over a list of nodes (empty → -1). */
template <typename TNodes, typename Key, typename TList>
struct TMaxLevel
{
	static constexpr int Value = -1;
};

template <typename TNodes, typename Key, typename THead, typename... TRest>
struct TMaxLevel<TNodes, Key, TTypeList<THead, TRest...>>
{
private:
	static constexpr int HeadLevel = TNodeLevel<TNodes, Key, THead>::Value;
	static constexpr int TailLevel = TMaxLevel<TNodes, Key, TTypeList<TRest...>>::Value;

public:
	static constexpr int Value = (HeadLevel > TailLevel) ? HeadLevel : TailLevel;
};

/** Filter a list to nodes whose level equals K. */
template <typename TNodes, typename Key, int K, typename TList>
struct TFilterByLevel;

template <typename TNodes, typename Key, int K>
struct TFilterByLevel<TNodes, Key, K, TTypeList<>>
{
	using Type = TTypeList<>;
};

template <typename TNodes, typename Key, int K, typename THead, typename... TRest>
struct TFilterByLevel<TNodes, Key, K, TTypeList<THead, TRest...>>
{
private:
	using FTail = typename TFilterByLevel<TNodes, Key, K, TTypeList<TRest...>>::Type;
	static constexpr bool bMatch = (TNodeLevel<TNodes, Key, THead>::Value == K);

public:
	using Type = std::conditional_t<
		bMatch,
		typename TCons<THead, FTail>::Type,
		FTail>;
};

/** One level: every node at level K (unordered — may run in parallel). */
template <typename TNodes, typename Key, int K>
struct TLevel
{
	using Type = typename TFilterByLevel<TNodes, Key, K, TNodes>::Type;
};

/** Levels 0..K as TTypeList<TTypeList<...>, ...>. */
template <typename TNodes, typename Key, int K>
struct TLevelsUpTo;

template <typename TNodes, typename Key>
struct TLevelsUpTo<TNodes, Key, 0>
{
	using Type = TTypeList<typename TLevel<TNodes, Key, 0>::Type>;
};

template <typename TNodes, typename Key, int K>
struct TLevelsUpTo
{
private:
	using FPrev = typename TLevelsUpTo<TNodes, Key, K - 1>::Type;
	using FLevelK = typename TLevel<TNodes, Key, K>::Type;

public:
	using Type = TAppend_t<FPrev, FLevelK>;
};

/**
 * Parallel levels of TNodes at Key: outer list = level sequence (level 0 runs
 * first), inner list = types within one level (mutually independent, may run
 * in parallel). Assumes acyclic (verify with TIsAcyclic_v).
 */
template <typename TNodes, typename Key, int Max, bool bEmpty>
struct TLevelsImpl;

template <typename TNodes, typename Key, int Max>
struct TLevelsImpl<TNodes, Key, Max, true>
{
	using Type = TTypeList<>;
};

template <typename TNodes, typename Key, int Max>
struct TLevelsImpl<TNodes, Key, Max, false>
{
	using Type = typename TLevelsUpTo<TNodes, Key, Max>::Type;
};

template <typename TNodes, typename Key>
struct TLevels
{
private:
	static constexpr int Max = TMaxLevel<TNodes, Key, TNodes>::Value;

public:
	using Type = typename TLevelsImpl<TNodes, Key, Max, (Max < 0)>::Type;
};

template <typename TNodes, typename Key>
using TLevels_t = typename TLevels<TNodes, Key>::Type;

} // namespace Topo

} // namespace Maho

// ───────────────────────────────────────────────────────────────────────
// MAHO_CLOSURE — read a code-gen closure (a pre-computed, deduplicated,
// acyclic dependency closure recorded per (Class, Key)).
//
// code-gen parses each class's MAHO_EXTEND_DEPS, builds the global graph,
// computes the transitive closure per (Class, Key), runs an acyclic pass, and
// writes a generated macro into the project's .gen.h:
//
//   #define MAHO_CLOSURE_0_SA_IA   ::Maho::TTypeList<>
//   #define MAHO_CLOSURE_0_SD_IA   ::Maho::TTypeList<SA, SB, SC>
//
// C++ users just write:
//
//   using FC = MAHO_CLOSURE(SD, IA);         // → TTypeList<SA, SB, SC>
//   using FL = Topo::TLevels_t<FC, IA>;      // correct bands (closure complete)
//   static_assert(Topo::TIsAcyclic_v<FC, IA>);   // closure is acyclic by code-gen
// ───────────────────────────────────────────────────────────────────────
#define MAHO_CLOSURE_CAT(A, B) MAHO_CLOSURE_CAT_I(A, B)
#define MAHO_CLOSURE_CAT_I(A, B) A##B
#define MAHO_CLOSURE(Class, Key) MAHO_CLOSURE_CAT(MAHO_CLOSURE_CAT(MAHO_CLOSURE_0_, Class), MAHO_CLOSURE_CAT(_, Key))

/**
 * Dependency-level bands for a Class at a Key, over its (code-gen) closure.
 * The closure already includes every mid-chain dependency, so leveling is
 * correct even for a partial aggregate. Users never see "closure" — just "the
 * level bands of SD when scheduled at IA".
 *
 *   using FLevels = MAHO_SORT_LEVEL(SD, IA);
 *   // == Topo::TLevels_t<MAHO_CLOSURE(SD, IA), IA>
 */
#define MAHO_SORT_LEVEL(Class, Key) \
	::Maho::Topo::TLevels_t<MAHO_CLOSURE(Class, Key), Key>