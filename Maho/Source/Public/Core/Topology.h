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
//   using FOrder = Topo::TTopoSort_t<FNodes, EEngineStage::Init>;
//   // FOrder == TTypeList<FMyBase, FMyInput, FMySystem>   (ordered, serial)
//   using FLevels = Topo::TLevels_t<FNodes, EEngineStage::Init>;
//   // FLevels == TTypeList<
//   //     TTypeList<FMyBase, FMyInput>,   (level 0, may run in parallel)
//   //     TTypeList<FMySystem>>           (level 1)
// ───────────────────────────────────────────────────────────────────────

template <auto SlotKey, typename TList>
struct TDependsOn;

template <typename... TSlots>
struct TDependsPack;

template <typename T, typename = void>
struct TResolveDependsPack;

template <typename T, auto Key>
struct TDependsList;

// ───────────────────────────────────────────────────────────────────────
// Declaration types.
// ───────────────────────────────────────────────────────────────────────

/**
 * One dependency slot: at Key, Self depends on every type in TList
 * (peers complete before Self).
 */
template <auto SlotKey, typename TList>
struct TDependsOn
{
	static constexpr auto Key = SlotKey;
	using FList = TList;
};

/** Variadic pack of TDependsOn slots. Inherit beside your interface. */
template <typename... TSlots>
struct TDependsPack
{
	using FDependsPack = TDependsPack;
	static constexpr std::size_t NumSlots = sizeof...(TSlots);

	/** Visit every slot: Visitor(Key, static_cast<TList*>(nullptr)). */
	template <typename TVisitor>
	static constexpr void ForEachSlot(TVisitor&& Visitor)
	{
		if constexpr (sizeof...(TSlots) > 0)
		{
			(Visitor(TSlots::Key, static_cast<typename TSlots::FList*>(nullptr)), ...);
		}
		else
		{
			(void)Visitor;
		}
	}
};

/** Resolve T's pack (empty when T declares no FDependsPack). */
template <typename T, typename>
struct TResolveDependsPack
{
	using Type = TDependsPack<>;
};

template <typename T>
struct TResolveDependsPack<T, std::void_t<typename T::FDependsPack>>
{
	using Type = typename T::FDependsPack;
};

// ───────────────────────────────────────────────────────────────────────
// Topo namespace: the compile-time algorithms.
// ───────────────────────────────────────────────────────────────────────
namespace Topo
{

/** Compile-time key equality (different types compare false, never ill-formed). */
template <auto A, auto B>
constexpr bool TKeyEqual()
{
	if constexpr (std::is_same_v<decltype(A), decltype(B)>)
	{
		return A == B;
	}
	return false;
}

/** Find the peer TTypeList for a Key inside a pack. */
template <typename TPack, auto Key>
struct TFindSlot
{
	using Type = TTypeList<>;
};

template <auto Key>
struct TFindSlot<TDependsPack<>, Key>
{
	using Type = TTypeList<>;
};

template <auto Key, auto SlotKey, typename TList, typename... TRest>
struct TFindSlot<TDependsPack<TDependsOn<SlotKey, TList>, TRest...>, Key>
{
	using Type = std::conditional_t<
		TKeyEqual<Key, SlotKey>(),
		TList,
		typename TFindSlot<TDependsPack<TRest...>, Key>::Type>;
};

/** Raw peer list of T at Key (no node filtering). */
template <typename T, auto Key>
struct TNodeDeps
{
	using Type = typename TFindSlot<typename TResolveDependsPack<T>::Type, Key>::Type;
};

template <typename T, auto Key>
using TNodeDeps_t = typename TNodeDeps<T, Key>::Type;

// ── ① Cycle detection (DFS path coloring; short-circuits on back edge). ─

template <typename TNodes, auto Key, typename TNode, typename TPath>
struct TDfsCycle;

template <typename TNodes, auto Key, typename TPath, typename TDeps>
struct TDfsCycleList;

template <typename TNodes, auto Key, typename TPath>
struct TDfsCycleList<TNodes, Key, TPath, TTypeList<>> : std::false_type
{
};

template <typename TNodes, auto Key, typename TPath, typename THead, typename... TRest>
struct TDfsCycleList<TNodes, Key, TPath, TTypeList<THead, TRest...>>
	: std::bool_constant<
		TDfsCycle<TNodes, Key, THead, TPath>::value
		|| TDfsCycleList<TNodes, Key, TPath, TTypeList<TRest...>>::value>
{
};

template <bool bInPath, typename TNodes, auto Key, typename TNode, typename TPath>
struct TDfsCycleImpl;

template <typename TNodes, auto Key, typename TNode, typename TPath>
struct TDfsCycleImpl<true, TNodes, Key, TNode, TPath> : std::true_type
{
};

template <typename TNodes, auto Key, typename TNode, typename TPath>
struct TDfsCycleImpl<false, TNodes, Key, TNode, TPath>
	: std::bool_constant<
		TDfsCycleList<
			TNodes,
			Key,
			typename TCons<TNode, TPath>::Type,
			typename TFilterDepsInNodes<TNodes, TNodeDeps_t<TNode, Key>>::Type>::value>
{
};

template <typename TNodes, auto Key, typename TNode, typename TPath>
struct TDfsCycle
{
	static constexpr bool bInPath = TContains_v<TPath, TNode>;
	static constexpr bool value = TDfsCycleImpl<bInPath, TNodes, Key, TNode, TPath>::value;
};

template <typename TNodes, auto Key>
struct THasCycleFromAny;

template <auto Key>
struct THasCycleFromAny<TTypeList<>, Key> : std::false_type
{
};

template <auto Key, typename THead, typename... TRest>
struct THasCycleFromAny<TTypeList<THead, TRest...>, Key>
	: std::bool_constant<
		TDfsCycle<TTypeList<THead, TRest...>, Key, THead, TTypeList<>>::value
		|| THasCycleFromAny<TTypeList<TRest...>, Key>::value>
{
};

template <typename TNodes, auto Key>
inline constexpr bool THasCycle_v = THasCycleFromAny<TNodes, Key>::value;

template <typename TNodes, auto Key>
inline constexpr bool TIsAcyclic_v = !THasCycle_v<TNodes, Key>;

/** static_assert that TNodes' graph at Key has no cycle. */
template <typename TNodes, auto Key>
constexpr void TAssertAcyclic()
{
	static_assert(
		TIsAcyclic_v<TNodes, Key>,
		"Topo: dependency graph has a cycle at this key");
}

// ── ② Topological sort (deps before dependents), DFS post-order. ───────

template <typename TVisited, typename TResult>
struct TTopoState
{
	using Visited = TVisited;
	using Result = TResult;
};

template <typename TNodes, auto Key, typename TList, typename TState>
struct TTopoVisitList;

template <typename TNodes, auto Key, typename TState>
struct TTopoVisitList<TNodes, Key, TTypeList<>, TState>
{
	using State = TState;
};

template <bool bVisited, typename TNodes, auto Key, typename TNode, typename TState>
struct TTopoVisitImpl;

template <typename TNodes, auto Key, typename TNode, typename TState>
struct TTopoVisitImpl<true, TNodes, Key, TNode, TState>
{
	using State = TState;
};

template <typename TNodes, auto Key, typename TNode, typename TState>
struct TTopoVisitImpl<false, TNodes, Key, TNode, TState>
{
private:
	using FDeps = typename TFilterDepsInNodes<TNodes, TNodeDeps_t<TNode, Key>>::Type;
	using FAfterDeps = typename TTopoVisitList<
		TNodes,
		Key,
		FDeps,
		TTopoState<typename TCons<TNode, typename TState::Visited>::Type, typename TState::Result>>::State;

public:
	using State = TTopoState<
		typename FAfterDeps::Visited,
		TAppend_t<typename FAfterDeps::Result, TNode>>;
};

template <typename TNodes, auto Key, typename TNode, typename TState>
struct TTopoVisit
{
	static constexpr bool bVisited = TContains_v<typename TState::Visited, TNode>;
	using State = typename TTopoVisitImpl<bVisited, TNodes, Key, TNode, TState>::State;
};

template <typename TNodes, auto Key, typename THead, typename... TRest, typename TState>
struct TTopoVisitList<TNodes, Key, TTypeList<THead, TRest...>, TState>
{
private:
	using FAfterHead = typename TTopoVisit<TNodes, Key, THead, TState>::State;

public:
	using State = typename TTopoVisitList<TNodes, Key, TTypeList<TRest...>, FAfterHead>::State;
};

/**
 * Topological order of TNodes at Key: a peer appears before its dependent.
 * Meaningful only for acyclic graphs (see TAssertAcyclic).
 */
template <typename TNodes, auto Key>
struct TTopoSort
{
	using Type = typename TTopoVisitList<
		TNodes, Key, TNodes, TTopoState<TTypeList<>, TTypeList<>>>::State::Result;
};

template <typename TNodes, auto Key>
using TTopoSort_t = typename TTopoSort<TNodes, Key>::Type;

// ── ③ Levels (parallel groups by dependency level). ─────────────────────

/**
 * Level of a node: 1 + max(level of deps); roots are 0.
 * Self-recursive: folds over the dep list, recursing into each dep's level.
 */
template <typename TNodes, auto Key, typename TNode>
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
		1 + FMaxLevel<typename TFilterDepsInNodes<TNodes, TNodeDeps_t<TNode, Key>>::Type, -1>::Value;
};

/** Max level over a list of nodes (empty → -1). */
template <typename TNodes, auto Key, typename TList>
struct TMaxLevel
{
	static constexpr int Value = -1;
};

template <typename TNodes, auto Key, typename THead, typename... TRest>
struct TMaxLevel<TNodes, Key, TTypeList<THead, TRest...>>
{
private:
	static constexpr int HeadLevel = TNodeLevel<TNodes, Key, THead>::Value;
	static constexpr int TailLevel = TMaxLevel<TNodes, Key, TTypeList<TRest...>>::Value;

public:
	static constexpr int Value = (HeadLevel > TailLevel) ? HeadLevel : TailLevel;
};

/** Filter a list to nodes whose level equals K. */
template <typename TNodes, auto Key, int K, typename TList>
struct TFilterByLevel;

template <typename TNodes, auto Key, int K>
struct TFilterByLevel<TNodes, Key, K, TTypeList<>>
{
	using Type = TTypeList<>;
};

template <typename TNodes, auto Key, int K, typename THead, typename... TRest>
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
template <typename TNodes, auto Key, int K>
struct TLevel
{
	using Type = typename TFilterByLevel<TNodes, Key, K, TNodes>::Type;
};

/** Levels 0..K as TTypeList<TTypeList<...>, ...>. */
template <typename TNodes, auto Key, int K>
struct TLevelsUpTo;

template <typename TNodes, auto Key>
struct TLevelsUpTo<TNodes, Key, 0>
{
	using Type = TTypeList<typename TLevel<TNodes, Key, 0>::Type>;
};

template <typename TNodes, auto Key, int K>
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
 * in parallel). Assumes acyclic (see TAssertAcyclic).
 */
template <typename TNodes, auto Key, int Max, bool bEmpty>
struct TLevelsImpl;

template <typename TNodes, auto Key, int Max>
struct TLevelsImpl<TNodes, Key, Max, true>
{
	using Type = TTypeList<>;
};

template <typename TNodes, auto Key, int Max>
struct TLevelsImpl<TNodes, Key, Max, false>
{
	using Type = typename TLevelsUpTo<TNodes, Key, Max>::Type;
};

template <typename TNodes, auto Key>
struct TLevels
{
private:
	static constexpr int Max = TMaxLevel<TNodes, Key, TNodes>::Value;

public:
	using Type = typename TLevelsImpl<TNodes, Key, Max, (Max < 0)>::Type;
};

template <typename TNodes, auto Key>
using TLevels_t = typename TLevels<TNodes, Key>::Type;

// ── ④ Reverse dependency: who depends on TTarget at Key. ───────────────

/**
 * Collect every type in TNodes whose dependency list at Key contains TTarget
 * (incoming edges of TTarget). Assumes the forward deps are already resolvable
 * via TNodeDeps (each type's TDependsPack).
 *
 *   using FWho = TFindDependents_t<FNodes, EPhase::Main, FC>;
 *   // FWho = TTypeList of every node that depends on FC at Main.
 */
template <typename TNodes, auto Key, typename TTarget>
struct TFindDependents;

template <auto Key, typename TTarget>
struct TFindDependents<TTypeList<>, Key, TTarget>
{
	using Type = TTypeList<>;
};

template <auto Key, typename THead, typename... TRest, typename TTarget>
struct TFindDependents<TTypeList<THead, TRest...>, Key, TTarget>
{
private:
	using FTail = typename TFindDependents<TTypeList<TRest...>, Key, TTarget>::Type;
	static constexpr bool bDepends = TContains_v<TNodeDeps_t<THead, Key>, TTarget>;

public:
	using Type = std::conditional_t<
		bDepends,
		typename TCons<THead, FTail>::Type,
		FTail>;
};

template <typename TNodes, auto Key, typename TTarget>
using TFindDependents_t = typename TFindDependents<TNodes, Key, TTarget>::Type;

// ── ⑤ Reverse a TTypeList (used for auto-mirrored shutdown order). ─────

/** Reverse a TTypeList: TTypeList<A, B, C> → TTypeList<C, B, A>. */
template <typename TList>
struct TReverse;

template <>
struct TReverse<TTypeList<>>
{
	using Type = TTypeList<>;
};

template <typename THead, typename... TRest>
struct TReverse<TTypeList<THead, TRest...>>
{
	using Type = TAppend_t<typename TReverse<TTypeList<TRest...>>::Type, THead>;
};

template <typename TList>
using TReverse_t = typename TReverse<TList>::Type;

} // namespace Topo

// ───────────────────────────────────────────────────────────────────────
// TDependsList: the deps of T at Key (empty if no pack / no matching slot).
// ───────────────────────────────────────────────────────────────────────

template <typename T, auto Key>
struct TDependsList
{
	using Type = typename Topo::TFindSlot<typename TResolveDependsPack<T>::Type, Key>::Type;
};

template <typename T, auto Key>
using TDependsList_t = typename TDependsList<T, Key>::Type;

} // namespace Maho
