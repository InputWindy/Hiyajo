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

template <typename T, typename = void>
struct TQueryTag
{
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
	[[nodiscard]] static const void* Get() noexcept
	{
		static const void* const Cached =
			::Maho::GetMetaData().Find(KDeclaredType, TTypeName<T>::value);
		return Cached;
	}
};

template <typename TFrom, typename TTo>
struct TQueryCast
{
	[[nodiscard]] static TTo* Get(TFrom* P) noexcept
	{
		(void)P;
		return nullptr;
	}
};

template <typename TSelf, typename... TTargets>
[[nodiscard]] inline void* QueryDispatch(TSelf* Self, const void* TargetTag) noexcept
{
	void* Found = nullptr;
	((TQueryTag<TTargets>::Get() == TargetTag && TargetTag != nullptr
		? (Found = static_cast<void*>(TQueryCast<TSelf, TTargets>::Get(Self)), true)
		: false) || ...);
	return Found;
}

template <typename TSelf, typename... TTargets>
[[nodiscard]] inline void* QueryDispatchStatic(TSelf* Self, const void* TargetTag) noexcept
{
	void* Found = nullptr;
	((TQueryTag<TTargets>::Get() == TargetTag && TargetTag != nullptr
		? (Found = static_cast<void*>(static_cast<TTargets*>(Self)), true)
		: false) || ...);
	return Found;
}

template <typename TSelf, typename... TTargets>
[[nodiscard]] inline void* QueryDispatchList(TSelf* Self, const void* TargetTag,
	TTypeList<TTargets...>) noexcept
{
	return QueryDispatchStatic<TSelf, TTargets...>(Self, TargetTag);
}

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

#define MAHO_QUERY_CASTER(Self, ...)                                                    \
	[[nodiscard]] void* QuerySubobject(const void* TargetTag) noexcept override          \
	{                                                                                    \
		return ::Maho::QueryDispatch<Self, __VA_ARGS__>(this, TargetTag);                 \
	}

template <typename TBase>
class FQueryResult
{
public:
	FQueryResult() = default;

	explicit FQueryResult(std::vector<TBase*>& InSource)
		: Source(&InSource)
	{
	}

	operator std::vector<TBase*>&() { return Data; }
	operator const std::vector<TBase*>&() const { return Data; }

	[[nodiscard]] std::size_t Num() const noexcept { return Data.size(); }
	[[nodiscard]] bool IsEmpty() const noexcept { return Data.empty(); }
	[[nodiscard]] const std::vector<TBase*>& GetData() const noexcept { return Data; }

	template <typename... TFilter>
	FQueryResult<TBase> Select() const
	{
		return Filter([](TBase* P) { return ((QueryCast<TBase, TFilter>(P) != nullptr) || ...); });
	}

	template <typename... TFilter>
	FQueryResult<TBase> With() const
	{
		return Filter([](TBase* P) { return ((QueryCast<TBase, TFilter>(P) != nullptr) && ...); });
	}

	template <typename... TFilter>
	FQueryResult<TBase> Not() const
	{
		return Filter([](TBase* P) { return !((QueryCast<TBase, TFilter>(P) != nullptr) || ...); });
	}

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

	static std::vector<TBase*>& EmptySource()
	{
		static std::vector<TBase*> Empty;
		return Empty;
	}

	std::vector<TBase*>  Data;
	std::vector<TBase*>* Source = nullptr;
};

template <typename TBase>
class FQuery
{
public:
	virtual ~FQuery() = default;

	virtual std::vector<TBase*>& GetQueryData() = 0;
	virtual const std::vector<TBase*>& GetQueryData() const = 0;

	template <typename... TFilter>
	FQueryResult<TBase> Select() const
	{
		return AsResult().template Select<TFilter...>();
	}

	template <typename... TFilter>
	FQueryResult<TBase> With() const
	{
		return AsResult().template With<TFilter...>();
	}

	template <typename... TFilter>
	FQueryResult<TBase> Not() const
	{
		return AsResult().template Not<TFilter...>();
	}

	template <typename T>
	[[nodiscard]] std::vector<T*> Cast() const
	{
		return AsResult().template Cast<T>();
	}

private:
	FQueryResult<TBase> AsResult() const
	{
		FQueryResult<TBase> Result(const_cast<std::vector<TBase*>&>(GetQueryData()));
		Result.Data = GetQueryData();
		return Result;
	}
};

}
