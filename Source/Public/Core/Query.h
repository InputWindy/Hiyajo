#pragma once

#include <Core/TypeList.h>

#include <type_traits>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Query — compile-time type filtering over a TTypeList.
//
// Query only selects types; it does not drive instances. Each result carries a
// filtered TTypeList (::Type) of every type deriving from all the With<...>
// bases and from none of the Not<...> bases. .With / .Not accumulate.
//
//   using FQuery = Maho::TQuery<FExtensions>
//       ::With<IRenderFeature>       // keep types deriving IRenderFeature
//       ::With<FTickableTag>         // AND deriving FTickableTag (accumulates)
//       ::Not<FHeadlessTag>;         // ... that don't derive FHeadlessTag;
//
//   using FMatched = FQuery::Type;   // a TTypeList<...> of matching types
//   static_assert(FMatched::Count > 0);
//
// Driving the matched types (by instance or singleton) is the host/scheduler's
// job — Query is pure type selection.
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

	/** Keep T when it derives all Required and none of Excluded. */
	template <typename T, typename TRequired, typename TExcluded>
	struct TFilter;

	template <typename TRequired, typename TExcluded>
	struct TFilter<TTypeList<>, TRequired, TExcluded>
	{
		using Type = TTypeList<>;
	};

	template <typename THead, typename... TRest, typename TRequired, typename TExcluded>
	struct TFilter<TTypeList<THead, TRest...>, TRequired, TExcluded>
	{
	private:
		using FTail = typename TFilter<TTypeList<TRest...>, TRequired, TExcluded>::Type;
		static inline constexpr bool kMatch =
			TDerivesAll<THead, TRequired>::value && !TDerivesAny<THead, TExcluded>::value;

	public:
		using Type = std::conditional_t<kMatch, typename TCons<THead, FTail>::Type, FTail>;
	};
}

// The query state — a candidate list + required/excluded base lists.
template <typename FCandidates, typename FRequired = TTypeList<>, typename FExcluded = TTypeList<>>
struct TQuery
{
	/** Every type matching the accumulated filters. */
	using Type = typename QueryDetail::TFilter<FCandidates, FRequired, FExcluded>::Type;

	/** Require T to also derive every TBases... (accumulates with prior With). */
	template <typename... TBases>
	using With = TQuery<FCandidates,
		typename TCatch<FRequired, TTypeList<TBases...>>::Type, FExcluded>;

	/** Exclude T deriving any TBases... (accumulates with prior Not). */
	template <typename... TBases>
	using Not = TQuery<FCandidates, FRequired,
		typename TCatch<FExcluded, TTypeList<TBases...>>::Type>;
};

} // namespace Maho
