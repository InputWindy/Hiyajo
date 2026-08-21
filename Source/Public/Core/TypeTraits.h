#pragma once

#include <type_traits>

namespace Maho
{

// Forward decl — TIsSingleton only needs the template, not a full definition.
template <typename TDerived>
class TSingleton;

// ───────────────────────────────────────────────────────────────────────
// TypeTraits — compile-time predicates on a TYPE (not runtime state).
//
// A predicate is a `template <typename T> struct` exposing ::value (typically
// derived from std::bool_constant). These are what Query::Where<T> accepts:
//
//   Query<Types>().Where<Not<TIsPod>>().Where<Or<TIsAbstract, TDerivesFrom<Base>>>();
//
// Predicates only see COMPILE-TIME type properties (derivation, membership,
// classification) — not runtime flags like "enabled". Use them to filter a
// type list before driving instances.
// ───────────────────────────────────────────────────────────────────────

/** True when T is a CRTP singleton (derives from TSingleton<T> itself). */
template <typename T, typename = void>
struct TIsSingleton : std::false_type
{
};

template <typename T>
struct TIsSingleton<T, std::enable_if_t<std::is_base_of_v<TSingleton<T>, T>>>
	: std::true_type
{
};

template <typename T>
inline constexpr bool TIsSingleton_v = TIsSingleton<T>::value;

// ───────────────────────────────────────────────────────────────────────
// Combinators — build composite predicates from predicates.
// ───────────────────────────────────────────────────────────────────────

/** P1 && P2 && ...  — every predicate's ::value must be true. */
template <template <typename> class... TPredicates>
struct TAnd
{
	template <typename T>
	struct Apply : std::conjunction<TPredicates<T>...>
	{
	};
};

/** P1 || P2 || ...  — at least one predicate's ::value must be true. */
template <template <typename> class... TPredicates>
struct TOr
{
	template <typename T>
	struct Apply : std::disjunction<TPredicates<T>...>
	{
	};
};

/** !P  — negate a predicate. */
template <template <typename> class TPredicate>
struct TNot
{
	template <typename T>
	struct Apply : std::negation<TPredicate<T>>
	{
	};
};

// ───────────────────────────────────────────────────────────────────────
// Identity / equality.
// ───────────────────────────────────────────────────────────────────────

/** True when T is exactly TTarget (no derivation — same type). */
template <typename TTarget>
struct TIs
{
	template <typename T>
	struct Apply : std::is_same<T, TTarget>
	{
	};
};

// ───────────────────────────────────────────────────────────────────────
// Derivation.
// ───────────────────────────────────────────────────────────────────────

/** True when T is TBase (std::is_base_of, includes the base itself). */
template <typename TBase>
struct TDerivesFrom
{
	template <typename T>
	struct Apply : std::is_base_of<TBase, T>
	{
	};
};

/** True when T is a strict derived of TBase (excludes the base itself). */
template <typename TBase>
struct TStrictlyDerivesFrom
{
	template <typename T>
	struct Apply
		: std::bool_constant<
			std::is_base_of_v<TBase, T> && !std::is_same_v<TBase, T>>
	{
	};
};

// ───────────────────────────────────────────────────────────────────────
// Classification (standard __is_x traits as predicates).
// ───────────────────────────────────────────────────────────────────────

/** True when T is a class/struct type. */
struct TIsClass
{
	template <typename T>
	struct Apply : std::is_class<T>
	{
	};
};

/** True when T is an enum type. */
struct TIsEnum
{
	template <typename T>
	struct Apply : std::is_enum<T>
	{
	};
};

/** True when T is an abstract type (has pure virtuals). */
struct TIsAbstract
{
	template <typename T>
	struct Apply : std::is_abstract<T>
	{
	};
};

/** True when T is trivially copyable. */
struct TIsTriviallyCopyable
{
	template <typename T>
	struct Apply : std::is_trivially_copyable<T>
	{
	};
};

} // namespace Maho
