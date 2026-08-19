#pragma once

#include <cstddef>
#include <type_traits>

namespace Maho
{

/**
 * Compile-time ordered list of types (a "type array").
 * Order matters: TTypeList<A, B> is a different type from TTypeList<B, A>.
 * Traversal order (serial / parallel) is the CALLER's choice, not encoded here.
 */
template <typename... TTypes>
struct TTypeList
{
	static constexpr std::size_t Count = sizeof...(TTypes);
};

/** Prepend T to the front of a TTypeList. */
template <typename T, typename TList>
struct TCons;

template <typename T, typename... Ts>
struct TCons<T, TTypeList<Ts...>>
{
	using Type = TTypeList<T, Ts...>;
};

/** Append T to the end of a TTypeList. */
template <typename TList, typename T>
struct TAppend;

template <typename... Ts, typename T>
struct TAppend<TTypeList<Ts...>, T>
{
	using Type = TTypeList<Ts..., T>;
};

template <typename TList, typename T>
using TAppend_t = typename TAppend<TList, T>::Type;

/** Membership: is T an element of TList? */
template <typename TList, typename T>
struct TContains : std::false_type
{
};

template <typename T, typename... TRest>
struct TContains<TTypeList<T, TRest...>, T> : std::true_type
{
};

template <typename THead, typename... TRest, typename T>
struct TContains<TTypeList<THead, TRest...>, T> : TContains<TTypeList<TRest...>, T>
{
};

template <typename TList, typename T>
inline constexpr bool TContains_v = TContains<TList, T>::value;

/**
 * List intersection: keep the elements of TDeps that also appear in TNodes,
 * preserving TDeps' order.
 */
template <typename TNodes, typename TDeps>
struct TFilterDepsInNodes;

template <typename TNodes>
struct TFilterDepsInNodes<TNodes, TTypeList<>>
{
	using Type = TTypeList<>;
};

template <typename TNodes, typename THead, typename... TRest>
struct TFilterDepsInNodes<TNodes, TTypeList<THead, TRest...>>
{
private:
	using FTail = typename TFilterDepsInNodes<TNodes, TTypeList<TRest...>>::Type;

public:
	using Type = std::conditional_t<
		TContains_v<TNodes, THead>,
		typename TCons<THead, FTail>::Type,
		FTail>;
};

// ───────────────────────────────────────────────────────────────────────
// Runtime traversal: ForEach unpacks a TTypeList and feeds each type to a
// Visitor, with a Scheduler controlling serial / parallel dispatch.
// ───────────────────────────────────────────────────────────────────────

/** Type tag: lets a generic callable recover T via decltype(Tag)::Type. */
template <typename T>
struct TTag
{
	using Type = T;
};

/**
 * ForEach scheduler contract: a callable object with Run(Callables...) —
 * serial / parallel are concrete schedulers, ForEach knows nothing about the
 * scheme. A scheduler may carry runtime state (e.g. FParallelScheduler holds
 * the thread pool it dispatches to).
 */
template <typename TScheduler>
concept FForEachScheduler = requires(TScheduler& S)
{
	S.Run([]{});
};

/**
 * Unpack a TTypeList, schedule per-type visits, and feed each type to Visitor
 * as a TTag<T>. TList is a compile-time policy (angle brackets); the scheduler
 * is a runtime value (parentheses) because it may carry state (the pool).
 *
 *   ForEach<FList>(FSerialScheduler{}, [](auto Tag, FWorld& W) {
 *       using T = typename decltype(Tag)::Type;
 *       // per-type work
 *   }, World);
 *
 *   ForEach<FList>(FParallelScheduler{Pool}, Visitor, World);
 */
template <typename TScheduler, typename TVisitor, typename... TArgs, typename... Ts>
void ForEachImpl(TTypeList<Ts...>, TScheduler&& Scheduler, TVisitor&& Visitor, TArgs&&... Args)
{
	Scheduler.Run([&] { Visitor(TTag<Ts>{}, Args...); }...);
}

template <typename TList, typename TScheduler, typename TVisitor, typename... TArgs>
	requires FForEachScheduler<TScheduler>
void ForEach(TScheduler&& Scheduler, TVisitor&& Visitor, TArgs&&... Args)
{
	ForEachImpl(TList{}, std::forward<TScheduler>(Scheduler), std::forward<TVisitor>(Visitor), std::forward<TArgs>(Args)...);
}

} // namespace Maho
