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
template <typename TList, typename TValue>
struct TAppend;

template <typename... Ts, typename TValue>
struct TAppend<TTypeList<Ts...>, TValue>
{
	using Type = TTypeList<Ts..., TValue>;
};

template <typename TList, typename TValue>
using TAppend_t = typename TAppend<TList, TValue>::Type;

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
 *   using FLayers = typename TFilter<TTypeList<A, B, C>, Maho::IAssembly>::Type;
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

/**
 * Filter a list by a predicate template: keep the elements where
 * TPredicate<T>::value is true, preserving order.
 *
 *   template <typename T> struct TIsSingleton;   // trait with ::value
 *   using FTools = typename TFilterWhere<TTypeList<A, B>, TIsSingleton>::Type;
 */
template <typename TList, template <typename> class TPredicate>
struct TFilterWhere;

template <template <typename> class TPredicate>
struct TFilterWhere<TTypeList<>, TPredicate>
{
	using Type = TTypeList<>;
};

template <typename THead, typename... TRest, template <typename> class TPredicate>
struct TFilterWhere<TTypeList<THead, TRest...>, TPredicate>
{
private:
	using FTail = typename TFilterWhere<TTypeList<TRest...>, TPredicate>::Type;

public:
	using Type = std::conditional_t<
		TPredicate<THead>::value,
		typename TCons<THead, FTail>::Type,
		FTail>;
};

// (Traversal — TTag / ForEach / the scheduler contract — lives in
//  Scheduler.h, where the drive protocol is defined.)

/**
 * Concatenate multiple TTypeLists into one, in order.
 *
 *   using FTags = TCatch<TTypeList<A>, TTypeList<B, C>>::Type;   // TTypeList<A, B, C>
 */
template <typename... TLists>
struct TCatch;

template <typename... T>
struct TCatch<TTypeList<T...>>
{
	using Type = TTypeList<T...>;
};

template <typename... T1, typename... T2, typename... TRest>
struct TCatch<TTypeList<T1...>, TTypeList<T2...>, TRest...>
{
	using Type = typename TCatch<TTypeList<T1..., T2...>, TRest...>::Type;
};

/**
 * Union of two TTypeLists (order-preserving, deduplicated). Fold appends each
 * element to an accumulator unless already present.
 */
namespace UnionDetail
{
	template <typename TAcc, typename TList>
	struct TFold;

	template <typename TAcc>
	struct TFold<TAcc, TTypeList<>>
	{
		using Type = TAcc;
	};

	template <typename TAcc, typename THead, typename... TRest>
	struct TFold<TAcc, TTypeList<THead, TRest...>>
	{
		using FAcc = std::conditional_t<TContains_v<TAcc, THead>, TAcc,
			typename TAppend<TAcc, THead>::Type>;
		using Type = typename TFold<FAcc, TTypeList<TRest...>>::Type;
	};
}

/** Union of two TTypeLists (order-preserving, deduplicated). */
template <typename TListA, typename TListB>
using TUnionList_t = typename UnionDetail::TFold<
	typename UnionDetail::TFold<TTypeList<>, TListA>::Type, TListB>::Type;

} // namespace Maho
