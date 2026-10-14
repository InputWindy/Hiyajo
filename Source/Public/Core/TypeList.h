/// @file
/// @brief Compile-time ordered type list (`TTypeList`) and its operations.
///
/// The vocabulary the whole engine filters with: a stage sequence, a mounted-interface set and a query
/// result are all "a list of types". Everything here is a value-less metafunction -- no storage, no
/// runtime cost -- and `TUnionList_t` preserves order while dropping duplicates, which is what lets a
/// frame's mounted stages be derived by merging lists instead of being declared twice.
#pragma once

#include <cstddef>
#include <type_traits>

namespace Maho
{

/// @brief A list of TYPES, as a type: `TTypeList<A, B, C>`.
/// @tparam TTypes The types in the list, in order.
template <typename... TTypes>
struct TTypeList
{
	/// @brief How many types the list holds.
	static constexpr std::size_t Count = sizeof...(TTypes);
};

/// @brief Prepends one type to a list (`TCons<T, TList>::Type`).
/// @tparam T The type to prepend.
/// @tparam TList The list to prepend to (a `TTypeList`).
template <typename T, typename TList>
struct TCons;

template <typename T, typename... Ts>
struct TCons<T, TTypeList<Ts...>>
{
	/// @brief The resulting list: `TTypeList<T, Ts...>`.
	using Type = TTypeList<T, Ts...>;
};

/// @brief Appends one type to a list (`TAppend<TList, TValue>::Type`).
/// @tparam TList The list to append to (a `TTypeList`).
/// @tparam TValue The type to append.
template <typename TList, typename TValue>
struct TAppend;

template <typename... Ts, typename TValue>
struct TAppend<TTypeList<Ts...>, TValue>
{
	/// @brief The resulting list: `TTypeList<Ts..., TValue>`.
	using Type = TTypeList<Ts..., TValue>;
};

/// @brief `TAppend` spelled as an alias template (the usual spelling).
/// @tparam TList The list to append to.
/// @tparam TValue The type to append.
template <typename TList, typename TValue>
using TAppend_t = typename TAppend<TList, TValue>::Type;

/// @brief Is `T` in the list? (the `_v` alias is what call sites read).
/// @tparam TList The list to search (a `TTypeList`).
/// @tparam T The type to look for.
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

/// @brief `TContains` as a value: `if constexpr (TContains_v<List, T>)`.
/// @tparam TList The list to search.
/// @tparam T The type to look for.
template <typename TList, typename T>
inline constexpr bool TContains_v = TContains<TList, T>::value;

/// @brief Concatenates any number of lists (`TCatch<L1, L2, ...>::Type`): order preserved, no dedup.
/// @tparam TLists The lists to concatenate, in order.
template <typename... TLists>
struct TCatch;

template <typename... T>
struct TCatch<TTypeList<T...>>
{
	/// @brief The concatenated list (one level: `TTypeList<T...>`).
	using Type = TTypeList<T...>;
};

template <typename... T1, typename... T2, typename... TRest>
struct TCatch<TTypeList<T1...>, TTypeList<T2...>, TRest...>
{
	/// @brief The concatenated list: the first two joined, then folded with the rest.
	using Type = typename TCatch<TTypeList<T1..., T2...>, TRest...>::Type;
};

namespace UnionDetail
{
	/// @brief One fold step of `TUnionList_t`: accumulate with "append unless already present".
	/// @tparam TAcc The list accumulated so far.
	/// @tparam TList The list still to consume.
	template <typename TAcc, typename TList>
	struct TFold;

	template <typename TAcc>
	struct TFold<TAcc, TTypeList<>>
	{
		/// @brief Nothing left to consume: the accumulator IS the result.
		using Type = TAcc;
	};

	template <typename TAcc, typename THead, typename... TRest>
	struct TFold<TAcc, TTypeList<THead, TRest...>>
	{
		/// @brief The accumulator after `THead` (appended only when it is new).
		using FAcc = std::conditional_t<TContains_v<TAcc, THead>, TAcc,
			typename TAppend<TAcc, THead>::Type>;

		/// @brief Continue with the rest of the list.
		using Type = typename TFold<FAcc, TTypeList<TRest...>>::Type;
	};
}

/// @brief ORDER-PRESERVING UNION of two lists: first-seen order, duplicates dropped.
///
/// This is the operation frames are assembled with -- a mount set is the union of what the frame and
/// its declared parts bring, and the same interface arriving twice must not run twice.
/// @tparam TListA The first list (its order wins for shared elements).
/// @tparam TListB The second list.
template <typename TListA, typename TListB>
using TUnionList_t = typename UnionDetail::TFold<
	typename UnionDetail::TFold<TTypeList<>, TListA>::Type, TListB>::Type;

/// @brief The stage list a frame MOUNTS, read off its `IPipeline<TStages...>` (`TFrame::TStages`).
///
/// A frame that mounts no pipeline (or a type that is not a frame at all) resolves to an EMPTY list
/// rather than failing to compile -- which is what makes the structural-edge declarations expand to
/// nothing for a frame with no stages, instead of being a special case at every call site.
/// @tparam TFrame The frame type to ask.
template <typename TFrame, typename = void>
struct TFrameStageList
{
	/// @brief No `TStages` on the type: an empty list.
	using Type = TTypeList<>;
};

template <typename TFrame>
struct TFrameStageList<TFrame, std::void_t<typename TFrame::TStages>>
{
	/// @brief The frame's mounted stages, in their declared order.
	using Type = typename TFrame::TStages;
};

}
