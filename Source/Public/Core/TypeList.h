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

/**
 * The stage list a type MOUNTS: `T::TStages`, i.e. whatever its `IPipeline<TStages...>` base
 * exposes -- or an EMPTY list for a type that mounts none.
 *
 * ONE definition, two consumers, and both of them need the same answer:
 *   - the trace's `StagePredecessorOf` (which stage of a frame runs before this one), and
 *   - the frame's own structural edge declarations (the in-frame chain and the per-stage self
 *     edge -- see MAHO_FRAME_STAGE_CHAIN / MAHO_FRAME_SELF_EDGES in Engine/Frame.h).
 * The list IS the frame's own declaration of what it mounts, so neither consumer is guessing: a
 * frame that mounts nothing (a host's lifecycle stages) gets "no predecessor" / "no edges" instead
 * of failing to compile.
 */
template <typename TFrame, typename = void>
struct TFrameStageList
{
	using Type = TTypeList<>;
};

template <typename TFrame>
struct TFrameStageList<TFrame, std::void_t<typename TFrame::TStages>>
{
	using Type = typename TFrame::TStages;
};

} // namespace Maho
