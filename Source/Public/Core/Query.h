#pragma once

// TQuery -- a type-UNRELATED compile-time LINQ over a type table. It never
// references instances / schedulers: input is a TTypeList, output is a
// filtered TTypeList. Select / With / Not are type-set operations evaluated at
// compile time; runtime driving of the surviving types is the caller's concern.
//
//   using FTable = TTypeList<FLog, FNet, FAudio>;
//   using FTickable = TQuery<FTable>::Select<ITick>::With<IShared>::Not<ITest>::FResult;
//
//   Select<T...>   keep types deriving ANY of T (OR)    -- starts from FList
//   With<T...>     keep types deriving ALL of T (AND)   -- refines FList
//   Not<T...>      drop types deriving ANY of T (NOR)   -- subtracts from FList
//
// The chain mutates FList: every call returns a NEW TQuery whose FList is the
// survivor set so far. FResult is just that running table.
//
// FQuery -- the RUNTIME counterpart: filters an instance vector by the same
// interface predicates (dynamic_cast on the erased base). Used when the layer
// set is dynamic (unknown at compile time).
//
//   std::vector<FLayerBase*> Layers = ...;
//   auto Tickable = FQuery<FLayerBase>(Layers).Select<IEngineTickPipeline>().Data;
#include <Core/TypeList.h>

#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Maho
{

// -- type-set operations (pure type algebra, no engine dependency) -----------------------

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
	/** The survivor set so far -- a TTypeList. */
	using FResult = FList;

	/** Keep types deriving ANY of TInterfaces... (OR) -- starts from FList. */
	template <typename... TInterfaces>
	[[nodiscard]] constexpr auto Select() const
	{
		return TQuery<typename QueryDetail::TKeep<FList, TInterfaces...>::Type>{};
	}

	/** Keep types deriving ALL of TInterfaces... (AND) -- refines FList. */
	template <typename... TInterfaces>
	[[nodiscard]] constexpr auto With() const
	{
		return TQuery<typename QueryDetail::TKeepAll<FList, TInterfaces...>::Type>{};
	}

	/** Drop types deriving ANY of TInterfaces... (NOR) -- subtracts from FList. */
	template <typename... TInterfaces>
	[[nodiscard]] constexpr auto Not() const
	{
		return TQuery<typename QueryDetail::TDrop<FList, TInterfaces...>::Type>{};
	}
};

/**
 * Runtime instance query result -- owns a filtered vector and is usable as a
 * vector via implicit conversion. Select/With/Not continue the chain on the
 * current result set (recursive querying).
 */
template <typename TBase>
class FQueryResult
{
public:
	std::vector<TBase*> Data;

	operator std::vector<TBase*>&() { return Data; }
	operator const std::vector<TBase*>&() const { return Data; }

	/** Keep instances whose dynamic type derives ANY of TFilter... (OR). */
	template <typename... TFilter>
	FQueryResult<TBase> Select() const
	{
		return Filter([](TBase* P) { return ((dynamic_cast<TFilter*>(P) != nullptr) || ...); });
	}

	/** Keep instances whose dynamic type derives ALL of TFilter... (AND). */
	template <typename... TFilter>
	FQueryResult<TBase> With() const
	{
		return Filter([](TBase* P) { return ((dynamic_cast<TFilter*>(P) != nullptr) && ...); });
	}

	/** Drop instances whose dynamic type derives ANY of TFilter... (NOR). */
	template <typename... TFilter>
	FQueryResult<TBase> Not() const
	{
		return Filter([](TBase* P) { return !((dynamic_cast<TFilter*>(P) != nullptr) || ...); });
	}

private:
	FQueryResult<TBase> Filter(std::function<bool(TBase*)> Pred) const
	{
		FQueryResult<TBase> Result;
		Result.Data.reserve(Data.size());
		for (TBase* Ptr : Data)
		{
			if (Ptr != nullptr && Pred(Ptr))
			{
				Result.Data.push_back(Ptr);
			}
		}
		return Result;
	}
};

/**
 * Runtime instance query -- an abstract query source over a vector of
 * erased-base pointers. The data source is supplied by the subclass via
 * GetQueryData() (e.g. FEngineBase returns its Pipelines vector). Select/With/
 * Not filter by interface predicates (dynamic_cast) and return value-type
 * FQueryResult instances.
 *
 *   // FEngineBase inherits FQuery<FLayerBase>; inside the engine:
 *   auto Tickable = Select<IEngineTickPipeline>();   // FQueryResult<FLayerBase>
 *   TickGraph.Init(Tickable);                        // implicit conversion to vector
 */
template <typename TBase>
class FQuery
{
public:
	virtual ~FQuery() = default;

	/** Data source: the subclass returns its instance collection. */
	virtual std::vector<TBase*>& GetQueryData() = 0;
	virtual const std::vector<TBase*>& GetQueryData() const = 0;

	/** Keep instances whose dynamic type derives ANY of TFilter... (OR). */
	template <typename... TFilter>
	FQueryResult<TBase> Select() const
	{
		return AsResult().template Select<TFilter...>();
	}

	/** Keep instances whose dynamic type derives ALL of TFilter... (AND). */
	template <typename... TFilter>
	FQueryResult<TBase> With() const
	{
		return AsResult().template With<TFilter...>();
	}

	/** Drop instances whose dynamic type derives ANY of TFilter... (NOR). */
	template <typename... TFilter>
	FQueryResult<TBase> Not() const
	{
		return AsResult().template Not<TFilter...>();
	}

private:
	FQueryResult<TBase> AsResult() const
	{
		FQueryResult<TBase> Result;
		Result.Data = GetQueryData();   // copy the source pointers
		return Result;
	}
};

} // namespace Maho
