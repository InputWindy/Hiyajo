#pragma once

// TQuery — a type-UNRELATED compile-time LINQ over a type table. It never
// references instances / schedulers: input is a TTypeList, output is a
// filtered TTypeList. Select / With / Not are type-set operations evaluated at
// compile time; runtime driving of the surviving types is the caller's concern.
//
//   using FTable = TTypeList<FLog, FNet, FAudio>;
//   using FTickable = TQuery<FTable>::Select<ITick>::With<IShared>::Not<ITest>::FResult;
//
//   Select<T...>   keep types deriving ANY of T (OR)    — starts from FList
//   With<T...>     keep types deriving ALL of T (AND)   — refines FList
//   Not<T...>      drop types deriving ANY of T (NOR)   — subtracts from FList
//
// The chain mutates FList: every call returns a NEW TQuery whose FList is the
// survivor set so far. FResult is just that running table.
#include <Core/TypeList.h>

#include <type_traits>

namespace Maho
{

// ── type-set operations (pure type algebra, no engine dependency) ─────────

namespace QueryDetail
{
	/** Keep elements of TList deriving ANY of TBase... (OR). */
	template <typename TList, typename... TBase>
	struct TKeep;
	template <typename... TBase>
	struct TKeep<TTypeList<>, TBase...> { using Type = TTypeList<>; };
	template <typename THead, typename... TRest, typename... TBase>
	struct TKeep<TTypeList<THead, TRest...>, TBase...>
	{
		static constexpr bool bAny = (std::is_base_of_v<TBase, THead> || ...);
		using FTail = typename TKeep<TTypeList<TRest...>, TBase...>::Type;
		using Type = std::conditional_t<bAny,
			typename TCons<THead, FTail>::Type, FTail>;
	};

	/** Keep elements of TList deriving ALL of TBase... (AND). */
	template <typename TList, typename... TBase>
	struct TKeepAll;
	template <typename... TBase>
	struct TKeepAll<TTypeList<>, TBase...> { using Type = TTypeList<>; };
	template <typename THead, typename... TRest, typename... TBase>
	struct TKeepAll<TTypeList<THead, TRest...>, TBase...>
	{
		static constexpr bool bAll = (std::is_base_of_v<TBase, THead> && ...);
		using FTail = typename TKeepAll<TTypeList<TRest...>, TBase...>::Type;
		using Type = std::conditional_t<bAll,
			typename TCons<THead, FTail>::Type, FTail>;
	};

	/** Drop elements of TList deriving ANY of TBase... (NOR / subtraction). */
	template <typename TList, typename... TBase>
	struct TDrop;
	template <typename... TBase>
	struct TDrop<TTypeList<>, TBase...> { using Type = TTypeList<>; };
	template <typename THead, typename... TRest, typename... TBase>
	struct TDrop<TTypeList<THead, TRest...>, TBase...>
	{
		static constexpr bool bAny = (std::is_base_of_v<TBase, THead> || ...);
		using FTail = typename TDrop<TTypeList<TRest...>, TBase...>::Type;
		using Type = std::conditional_t<bAny, FTail,
			typename TCons<THead, FTail>::Type>;
	};
}

/**
 * Type-agnostic compile-time query over a type table.
 *
 * Select / With / Not are chainable and pure: they never touch runtime state.
 * Each returns a new TQuery whose FList is the survivor set so far; FResult is
 * that running table. Driving the surviving elements is left to the caller.
 *
 *   TQuery<FTable> Q;
 *   using FOut = decltype(Q.Select<IA>().With<IB>().Not<IC>())::FResult;
 */
template <typename FList>
class TQuery
{
public:
	/** The survivor set so far — a TTypeList. */
	using FResult = FList;

	/** Keep types deriving ANY of TInterfaces... (OR) — starts from FList. */
	template <typename... TInterfaces>
	[[nodiscard]] constexpr auto Select() const
	{
		return TQuery<typename QueryDetail::TKeep<FList, TInterfaces...>::Type>{};
	}

	/** Keep types deriving ALL of TInterfaces... (AND) — refines FList. */
	template <typename... TInterfaces>
	[[nodiscard]] constexpr auto With() const
	{
		return TQuery<typename QueryDetail::TKeepAll<FList, TInterfaces...>::Type>{};
	}

	/** Drop types deriving ANY of TInterfaces... (NOR) — subtracts from FList. */
	template <typename... TInterfaces>
	[[nodiscard]] constexpr auto Not() const
	{
		return TQuery<typename QueryDetail::TDrop<FList, TInterfaces...>::Type>{};
	}
};

} // namespace Maho
