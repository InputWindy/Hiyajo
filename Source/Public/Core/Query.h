/// @file
/// @brief Querying one object for another CAPABILITY it also implements, without RTTI.
///
/// This is the mechanism that replaces `dynamic_cast` in the engine: a DECLARED type's identity is the
/// address of the row its `MAHO_DECLARE_TYPE` filed in the store, so "is this thing also an X?" is a
/// token compare plus one virtual call, and a type that declared nothing simply answers nullptr.
/// A frame declares itself with `MAHO_QUERY_CASTER`; consumers ask through `QueryCast` or a
/// `FQuery`/`FQueryResult` filter chain.
#pragma once

#include <Core/Fatal.h>
#include <Core/MetaData.h>
#include <Core/TypeList.h>

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Maho
{

/// @brief The identity token of a type, for the query protocol: `nullptr` UNLESS the type declared
///        itself (`MAHO_DECLARE_TYPE`), which is what makes "unknown type" the safe default.
/// @tparam T The type to ask about.
template <typename T, typename = void>
struct TQueryTag
{
	/// @brief No declaration, no identity: the token is `nullptr` and every query for it fails closed.
	[[nodiscard]] static const void* Get() noexcept
	{
		return nullptr;
	}
};

/** A DECLARED type (`MAHO_DECLARE_TYPE`): its identity is the ADDRESS of the row its declaration
 *  filed in the store -- one value, resolved identically in every module, and no name comparison at
 *  the point of use (the lookup happens once per type per module, then it is a pointer compare). */
template <typename T>
struct TQueryTag<T, std::enable_if_t<!TTypeName<T>::value.empty()>>
{
	/// @brief The row's address, resolved once per module (the `static` is the whole point).
	[[nodiscard]] static const void* Get() noexcept
	{
		static const void* const Cached =
			::Maho::GetMetaData().Find(KDeclaredType, TTypeName<T>::value);
		return Cached;
	}
};

/// @brief The per-type CAST answer: specialised BY THE TYPE (or by its module) to say "a `TFrom` is
///        also a `TTo`". The default is "no": an undeclared relationship answers `nullptr`.
/// @tparam TFrom The type asked from.
/// @tparam TTo The type asked for.
template <typename TFrom, typename TTo>
struct TQueryCast
{
	/// @brief No relationship declared: this object is not a `TTo`.
	/// @param P Ignored (the default answers without looking at the object).
	/// @return Always `nullptr`.
	[[nodiscard]] static TTo* Get(TFrom* P) noexcept
	{
		(void)P;
		return nullptr;
	}
};

/// @brief The `QuerySubobject` body a querying frame uses: return the FIRST declared target whose
///        token matches (a `nullptr` tag never matches).
///
/// Written as a recursive overload pair rather than a fold expression with an assignment and a comma
/// inside it: the semantics are identical (first match wins, nothing is evaluated after it), and this
/// shape is what a plain C++ parser reads -- the fold cost us the rest of this file's API in the
/// generated docs (Tools/docs_scan.py reports the parse error, it does not hide it).
/// @tparam TSelf The frame's own type (the `TFrom` of each cast).
/// @param Self The frame instance to cast from.
/// @param TargetTag The token asked for.
/// @return The matching subobject, or `nullptr`.
template <typename TSelf>
[[nodiscard]] inline void* QueryDispatch(TSelf*, const void*) noexcept
{
	return nullptr;                          // 没有目标可用
}

/// @brief One step of the walk: this head, else the rest.
/// @tparam TSelf The frame's own type.
/// @tparam THead The capability being tried now.
/// @tparam TRest The capabilities left.
/// @param Self The frame instance.
/// @param TargetTag The token asked for.
/// @return The matching subobject, or `nullptr`.
template <typename TSelf, typename THead, typename... TRest>
[[nodiscard]] inline void* QueryDispatch(TSelf* Self, const void* TargetTag) noexcept
{
	if (TargetTag == nullptr)
	{
		return nullptr;
	}
	if (TQueryTag<THead>::Get() == TargetTag)
	{
		return static_cast<void*>(TQueryCast<TSelf, THead>::Get(Self));
	}
	return QueryDispatch<TSelf, TRest...>(Self, TargetTag);
}

/// @brief Same walk, for targets the frame IS (a base subobject) rather than targets it hands out:
///        the cast is a plain `static_cast`, so `MAHO_QUERY_CASTER` can name bare interfaces.
/// @tparam TSelf The frame's own type.
/// @param Self The frame instance.
/// @param TargetTag The token asked for.
/// @return The matching subobject, or `nullptr`.
template <typename TSelf>
[[nodiscard]] inline void* QueryDispatchStatic(TSelf*, const void*) noexcept
{
	return nullptr;
}

/// @brief One step of the static walk: this head, else the rest.
/// @tparam TSelf The frame's own type.
/// @tparam THead The interface being tried now.
/// @tparam TRest The interfaces left.
/// @param Self The frame instance.
/// @param TargetTag The token asked for.
/// @return The matching subobject, or `nullptr`.
template <typename TSelf, typename THead, typename... TRest>
[[nodiscard]] inline void* QueryDispatchStatic(TSelf* Self, const void* TargetTag) noexcept
{
	if (TargetTag == nullptr)
	{
		return nullptr;
	}
	if (TQueryTag<THead>::Get() == TargetTag)
	{
		return static_cast<void*>(static_cast<THead*>(Self));
	}
	return QueryDispatchStatic<TSelf, TRest...>(Self, TargetTag);
}

/// @brief `QueryDispatchStatic` taking the target list AS A `TTypeList` value (what a frame's
///        `TFrameStageList` / `TStages` hands over).
/// @tparam TSelf The frame's own type.
/// @tparam TTargets The interfaces in the list.
/// @param Self The frame instance.
/// @param TargetTag The token asked for.
/// @param Unused The list, carried as a value so the types can be deduced.
/// @return The matching subobject, or `nullptr`.
template <typename TSelf, typename... TTargets>
[[nodiscard]] inline void* QueryDispatchList(TSelf* Self, const void* TargetTag,
	TTypeList<TTargets...>) noexcept
{
	return QueryDispatchStatic<TSelf, TTargets...>(Self, TargetTag);
}

/// @brief Asks an object whether it is also a `TTo` -- the `dynamic_cast` replacement.
/// @tparam TBase The type being asked from.
/// @tparam TTo The type being asked for.
/// @param P The object (`nullptr` is answered with `nullptr`, never dereferenced).
/// @return The `TTo` view, or `nullptr` when the object does not offer it.
template <typename TBase, typename TTo>
[[nodiscard]] inline TTo* QueryCast(TBase* P) noexcept
{
	const void* Tag = TQueryTag<TTo>::Get();
	if (P == nullptr || Tag == nullptr)
	{
		return nullptr;
	}
	return static_cast<TTo*>(P->QuerySubobject(Tag));
}

/// @brief Declares a frame's answer to the query protocol: `QuerySubobject` over the listed targets.
/// @param Self The frame type (`this`'s type).
/// @param ... The capabilities this frame offers (interfaces it implements, or types it casts to).
#define MAHO_QUERY_CASTER(Self, ...)                                                    \
	[[nodiscard]] void* QuerySubobject(const void* TargetTag) noexcept override          \
	{                                                                                    \
		return ::Maho::QueryDispatch<Self, __VA_ARGS__>(this, TargetTag);                 \
	}

/// @brief A filtered VIEW of a container's elements: the chain is built with `Select` / `With` / `Not`
///        and materialised with `Cast`.
/// @tparam TBase The element type the source container holds.
template <typename TBase>
class FQueryResult
{
public:
	/// @brief An empty result (no source to validate against).
	FQueryResult() = default;

	/// @brief A result over `InSource`, kept as the liveness oracle for the pointers it hands out.
	/// @param InSource The container the result's pointers came from (outlives this result).
	explicit FQueryResult(std::vector<TBase*>& InSource)
		: Source(&InSource)
	{
	}

	/// @brief The raw result as a container, so a caller can use it where a `std::vector<TBase*>` is
	///        expected without writing `.GetData()`.
	/// @return The result's elements (mutable).
	operator std::vector<TBase*>&() { return Data; }

	/// @brief Same, read-only.
	/// @return The result's elements.
	operator const std::vector<TBase*>&() const { return Data; }

	/// @brief How many elements the result holds.
	[[nodiscard]] std::size_t Num() const noexcept { return Data.size(); }

	/// @brief True when the result holds nothing.
	[[nodiscard]] bool IsEmpty() const noexcept { return Data.empty(); }

	/// @brief The raw pointers, for callers that want to iterate without materialising a typed vector.
	[[nodiscard]] const std::vector<TBase*>& GetData() const noexcept { return Data; }

	/// @brief Keeps the elements that offer AT LEAST ONE of the listed capabilities (OR).
	/// @tparam TFilter The capabilities to test for.
	/// @return A new result (the receiver is unchanged).
	template <typename... TFilter>
	FQueryResult<TBase> Select() const
	{
		return Filter([](TBase* P) { return ((QueryCast<TBase, TFilter>(P) != nullptr) || ...); });
	}

	/// @brief Keeps the elements that offer ALL of the listed capabilities (AND).
	/// @tparam TFilter The capabilities to require.
	/// @return A new result (the receiver is unchanged).
	template <typename... TFilter>
	FQueryResult<TBase> With() const
	{
		return Filter([](TBase* P) { return ((QueryCast<TBase, TFilter>(P) != nullptr) && ...); });
	}

	/// @brief Keeps the elements that offer NONE of the listed capabilities.
	/// @tparam TFilter The capabilities to exclude.
	/// @return A new result (the receiver is unchanged).
	template <typename... TFilter>
	FQueryResult<TBase> Not() const
	{
		return Filter([](TBase* P) { return !((QueryCast<TBase, TFilter>(P) != nullptr) || ...); });
	}

	/// @brief MATERIALISES the result as typed pointers -- the one step that can drop an entry (a
	///        pointer whose layer was unloaded since the chain was built is reported and skipped).
	/// @tparam T The type to hand back.
	/// @return The elements that are also a `T`, in the source's order.
	template <typename T>
	[[nodiscard]] std::vector<T*> Cast() const
	{
		std::vector<T*> Out;
		Out.reserve(Data.size());
		for (TBase* P : Data)
		{
			if (!CheckStillLive(P))
			{
				continue;
			}
			if (T* Typed = QueryCast<TBase, T>(P))
			{
				Out.push_back(Typed);
			}
		}
		return Out;
	}

private:
	template <typename T>
	friend class FQuery;

	/// @brief Is this pointer still in the source container? (See the body: an unloaded layer leaves a
	///        dangling pointer behind, and handing it out is the one failure this guard prevents.)
	/// @param Ptr The pointer to check.
	/// @return True when it may be used.
	[[nodiscard]] bool CheckStillLive(TBase* Ptr) const
	{
#if MAHO_DO_CONTAINER_CHECKS
		if (Ptr == nullptr)
		{
			return false;
		}
		if (Source != nullptr)
		{
			for (const TBase* Live : *Source)
			{
				if (Live == Ptr)
				{
					return true;
				}
			}
			ReportError("query result holds a pointer that is no longer in its source (the layer was "
				"unloaded); the entry was dropped");
			return false;
		}
		return true;
#else
		return Ptr != nullptr;
#endif
	}

	/// @brief The shared body of `Select` / `With` / `Not`.
	/// @tparam TPred The predicate type (a lambda over an element pointer).
	/// @param Pred The predicate to keep elements with.
	/// @return The filtered result.
	template <typename TPred>
	FQueryResult<TBase> Filter(TPred Pred) const
	{
		FQueryResult<TBase> Result(Source != nullptr ? *Source : EmptySource());
		Result.Data.reserve(Data.size());
		for (TBase* Ptr : Data)
		{
			if (CheckStillLive(Ptr) && Pred(Ptr))
			{
				Result.Data.push_back(Ptr);
			}
		}
		return Result;
	}

	/// @brief The source a default-constructed result validates against (nothing to validate).
	/// @return A shared empty container.
	static std::vector<TBase*>& EmptySource()
	{
		static std::vector<TBase*> Empty;
		return Empty;
	}

	/// @brief The elements this result currently holds (the chain narrows it step by step).
	std::vector<TBase*>  Data;

	/// @brief The container the elements came from: the liveness oracle `CheckStillLive` validates
	///        against (`nullptr` when there is nothing to validate against).
	std::vector<TBase*>* Source = nullptr;
};

/// @brief The ABSTRACT side of the same chain: a container that can hand out its element list
///        (`GetQueryData`), so `Select` / `With` / `Not` / `Cast` read the same on it.
/// @tparam TBase The element type.
template <typename TBase>
class FQuery
{
public:
	/// @brief Polymorphic base: destroyed through a `FQuery<TBase>*`.
	virtual ~FQuery() = default;

	/// @brief The elements, mutable (the owner's own list).
	/// @return The container this query walks.
	virtual std::vector<TBase*>& GetQueryData() = 0;

	/// @brief The elements, read-only.
	/// @return The container this query walks.
	virtual const std::vector<TBase*>& GetQueryData() const = 0;

	/// @brief See `FQueryResult::Select`.
	/// @tparam TFilter The capabilities to test for.
	/// @return A new result (the receiver is unchanged).
	template <typename... TFilter>
	FQueryResult<TBase> Select() const
	{
		return AsResult().template Select<TFilter...>();
	}

	/// @brief See `FQueryResult::With`.
	/// @tparam TFilter The capabilities to require.
	/// @return A new result (the receiver is unchanged).
	template <typename... TFilter>
	FQueryResult<TBase> With() const
	{
		return AsResult().template With<TFilter...>();
	}

	/// @brief See `FQueryResult::Not`.
	/// @tparam TFilter The capabilities to exclude.
	/// @return A new result (the receiver is unchanged).
	template <typename... TFilter>
	FQueryResult<TBase> Not() const
	{
		return AsResult().template Not<TFilter...>();
	}

	/// @brief See `FQueryResult::Cast`.
	/// @tparam T The type to hand back.
	/// @return The matching elements, in the source's order.
	template <typename T>
	[[nodiscard]] std::vector<T*> Cast() const
	{
		return AsResult().template Cast<T>();
	}

private:
	/// @brief This query as a result (the chain starts here).
	/// @return A result over the same elements.
	FQueryResult<TBase> AsResult() const
	{
		FQueryResult<TBase> Result(const_cast<std::vector<TBase*>&>(GetQueryData()));
		Result.Data = GetQueryData();
		return Result;
	}
};

}
