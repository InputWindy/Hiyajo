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

/**
 * Filter a list by base class: keep the elements deriving from TBase,
 * preserving order (std::is_base_of inlined — no predicate alias needed).
 *
 *   using FT typename TFilter<TTypeList<A, B, C>, FToolTag>::Type;
 *   using FLayers = typename TFilter<TTypeList<A, B, C>, FLayerTag>::Type;
 */
template <typename TList, typename TBase>
struct TFilter;

template <typename TBase>
struct TFilter<TTypeList<>, TBase>
{
	using Type = TTypeList<>;
};

template <typename THead, typename... TRest, typename TBase>
struct TFilter<TTypeList<THead, TRest...>, TBase>
{
private:
	using FTail = typename TFilter<TTypeList<TRest...>, TBase>::Type;

public:
	using Type = std::conditional_t<
		std::is_base_of_v<TBase, THead>,
		typename TCons<THead, FTail>::Type,
		FTail>;
};

// (Traversal — TTag / ForEach / the scheduler contract — lives in
//  Scheduler.h, where the drive protocol is defined.)

} // namespace Maho
