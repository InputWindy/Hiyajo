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

} // namespace Maho
