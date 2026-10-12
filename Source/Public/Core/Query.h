#pragma once

#include <Core/Fatal.h>

#include <cstddef>
#include <vector>

namespace Maho
{

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
		return Filter([](TBase* P) { return ((dynamic_cast<TFilter*>(P) != nullptr) || ...); });
	}

	template <typename... TFilter>
	FQueryResult<TBase> With() const
	{
		return Filter([](TBase* P) { return ((dynamic_cast<TFilter*>(P) != nullptr) && ...); });
	}

	template <typename... TFilter>
	FQueryResult<TBase> Not() const
	{
		return Filter([](TBase* P) { return !((dynamic_cast<TFilter*>(P) != nullptr) || ...); });
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
			if (auto* Tp = dynamic_cast<T*>(P))
			{
				Out.push_back(Tp);
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
			ReportError("query result holds a pointer that is no longer in its source "
				"(the layer was unloaded); the entry was dropped");
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
	template <typename T>
	friend class FQuery;

	FQueryResult<TBase> AsResult() const
	{
		FQueryResult<TBase> Result(const_cast<std::vector<TBase*>&>(GetQueryData()));
		Result.Data = GetQueryData();
		return Result;
	}
};

}
