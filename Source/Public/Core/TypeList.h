#pragma once

#include <cstddef>
#include <type_traits>

namespace Maho
{

template <typename... TTypes>
struct TTypeList
{
	static constexpr std::size_t Count = sizeof...(TTypes);
};

template <typename T, typename TList>
struct TCons;

template <typename T, typename... Ts>
struct TCons<T, TTypeList<Ts...>>
{
	using Type = TTypeList<T, Ts...>;
};

template <typename TList, typename TValue>
struct TAppend;

template <typename... Ts, typename TValue>
struct TAppend<TTypeList<Ts...>, TValue>
{
	using Type = TTypeList<Ts..., TValue>;
};

template <typename TList, typename TValue>
using TAppend_t = typename TAppend<TList, TValue>::Type;

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

template <typename TListA, typename TListB>
using TUnionList_t = typename UnionDetail::TFold<
	typename UnionDetail::TFold<TTypeList<>, TListA>::Type, TListB>::Type;

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

}
