#pragma once

#include <Core/TypeList.h>

#include <type_traits>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Query — a compile-time type filter over a TTypeList (LINQ-style).
//
//   constexpr auto Q = Maho::Query<TTypeList<Exts...>>()   // FROM types
//       .Select<IRenderFeature>()                          // WHERE derives IRenderFeature
//       .Where<Not<TIsSingleton>>()                        //   AND NOT (predicate)
//       .Cast<IRenderFeature>();                            // assert + finalize
//
//   using FMatched = decltype(Q);                          // TTypeList<...>
//   static_assert(FMatched::Count > 0);
//
// - Select<TBases...>: keep types deriving every TBases. This is the most
//   common filter (interface query) — a shorthand for Where<TDerivesFrom<T>>.
// - Where<TFilters...>: keep types where every filter's Apply<T>::value is true.
//   Filters are type predicates carrying `template<typename T> struct Apply`;
//   Not<TPredicate> negates a predicate template.
// - Cast<TBase>: asserts every survivor derives TBase, returns the filtered
//   TTypeList (a value, usable in ForEach<TList> and Topo::TTopoSort_t<TList, Key>).
//
// Query only selects types — it never drives instances.
// ───────────────────────────────────────────────────────────────────────

namespace QueryDetail
{
	template <typename T, typename TList>
	struct TDerivesAll;

	template <typename T>
	struct TDerivesAll<T, TTypeList<>> : std::true_type
	{
	};

	template <typename T, typename THead, typename... TRest>
	struct TDerivesAll<T, TTypeList<THead, TRest...>>
		: std::conjunction<
			std::is_base_of<THead, T>,
			TDerivesAll<T, TTypeList<TRest...>>>
	{
	};

	template <typename T, typename TList>
	struct TDerivesAny;

	template <typename T>
	struct TDerivesAny<T, TTypeList<>> : std::false_type
	{
	};

	template <typename T, typename THead, typename... TRest>
	struct TDerivesAny<T, TTypeList<THead, TRest...>>
		: std::disjunction<
			std::is_base_of<THead, T>,
			TDerivesAny<T, TTypeList<TRest...>>>
	{
	};

	/** Does every type in TList derive TBase? */
	template <typename TList, typename TBase>
	struct TAllDerive;

	template <typename TBase>
	struct TAllDerive<TTypeList<>, TBase> : std::true_type
	{
	};

	template <typename THead, typename... TRest, typename TBase>
	struct TAllDerive<TTypeList<THead, TRest...>, TBase>
		: std::conjunction<std::is_base_of<TBase, THead>,
			TAllDerive<TTypeList<TRest...>, TBase>>
	{
	};

	/** Does T satisfy every filter (Apply<T>::value true)? */
	template <typename T, typename TFilterList>
	struct TSatisfiesAll;

	template <typename T>
	struct TSatisfiesAll<T, TTypeList<>> : std::true_type
	{
	};

	template <typename T, typename THead, typename... TRest>
	struct TSatisfiesAll<T, TTypeList<THead, TRest...>>
		: std::conjunction<
			typename THead::template Apply<T>,
			TSatisfiesAll<T, TTypeList<TRest...>>>
	{
	};

	/** Keep T when it derives all Required, none Excluded, satisfies all Filters. */
	template <typename T, typename TRequired, typename TExcluded, typename TFilterList>
	struct TFilter;

	template <typename TRequired, typename TExcluded, typename TFilterList>
	struct TFilter<TTypeList<>, TRequired, TExcluded, TFilterList>
	{
		using Type = TTypeList<>;
	};

	template <typename THead, typename... TRest,
		typename TRequired, typename TExcluded, typename TFilterList>
	struct TFilter<TTypeList<THead, TRest...>, TRequired, TExcluded, TFilterList>
	{
	private:
		using FTail = typename TFilter<TTypeList<TRest...>, TRequired, TExcluded, TFilterList>::Type;
		static inline constexpr bool kMatch =
			TDerivesAll<THead, TRequired>::value
			&& !TDerivesAny<THead, TExcluded>::value
			&& TSatisfiesAll<THead, TFilterList>::value;

	public:
		using Type = std::conditional_t<kMatch, typename TCons<THead, FTail>::Type, FTail>;
	};
}

/** A predicate filter: keeps T where TPredicate<T>::value is true. */
template <template <typename> class TPredicate>
struct TWhere
{
	template <typename T>
	struct Apply : TPredicate<T>
	{
	};
};

/** A negated predicate filter: keeps T where TPredicate<T>::value is false. */
template <template <typename> class TPredicate>
struct Not
{
	template <typename T>
	struct Apply : std::negation<TPredicate<T>>
	{
	};
};

// The query value — chains .Select / .Where, resolves to the filtered list.
template <typename FCandidates, typename FRequired = TTypeList<>,
	typename FExcluded = TTypeList<>, typename FFilters = TTypeList<>>
struct TQuery
{
	/** Every type matching the accumulated filters. */
	using Type = typename QueryDetail::TFilter<FCandidates, FRequired, FExcluded, FFilters>::Type;

	/** Require T to derive every TBases... (accumulates). */
	template <typename... TBases>
	[[nodiscard]] constexpr auto Select() const
	{
		return TQuery<FCandidates,
			typename TCatch<FRequired, TTypeList<TBases...>>::Type, FExcluded, FFilters>{};
	}

	/** Exclude T deriving any TBases... (accumulates). */
	template <typename... TBases>
	[[nodiscard]] constexpr auto Exclude() const
	{
		return TQuery<FCandidates, FRequired,
			typename TCatch<FExcluded, TTypeList<TBases...>>::Type, FFilters>{};
	}

	/** Require every filter's Apply<T>::value (accumulates). */
	template <typename... TFilters>
	[[nodiscard]] constexpr auto Where() const
	{
		return TQuery<FCandidates, FRequired, FExcluded,
			typename TCatch<FFilters, TTypeList<TFilters...>>::Type>{};
	}

	/** Finalize (LINQ Cast): verify every survivor derives TBase, return the filtered TTypeList. */
	template <typename TBase>
	[[nodiscard]] constexpr auto Cast() const
	{
		static_assert(QueryDetail::TAllDerive<Type, TBase>::value,
			"Query::Cast<TBase> requires every selected type to derive TBase");
		return Type{};
	}
};

/** Build a query over a type list. */
template <typename FList>
[[nodiscard]] constexpr auto Query()
{
	return TQuery<FList>{};
}

} // namespace Maho
