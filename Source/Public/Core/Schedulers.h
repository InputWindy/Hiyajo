#pragma once

#include <Core/TypeList.h>
#include <Core/Topology.h>
#include <Core/Singleton.h>
#include <Core/ThreadPool.h>

#include <concepts>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// The drive protocol: the traversal machinery + the scheduler contract.
//
//   forall FForEachScheduler   — "S has Run(Callables...)"
//   ForEach / TTag             — unpacks a TTypeList to per-type visits
//   IScheduler                 — Run (stage drive)
//
// There is NO extension-interaction protocol beside the visitor lambda: the
// scheduler only traverses lists (compile-time) or layer instances (runtime)
// and hands each target to a visitor the host passes in. Layers expose only
// capability methods; the host decides what each stage does in the lambda.
//
// Lives in Engine: it dispatches over FLayerBase (Engine/Layer.h), the
// polymorphic layer node — Core stays limited to pure type/concurrency
// primitives.
// ───────────────────────────────────────────────────────────────────────

/** Type tag: lets a generic callable recover T via decltype(Tag)::Type. */
template <typename T>
struct TTag
{
	using Type = T;
};

/** ForEach scheduler contract: a callable with Run(Callables...). */
template <typename TScheduler>
concept FForEachScheduler = requires(TScheduler& S)
{
	S.Run([]{});
};

/** Serial traversal — run every callable in order (no thread, stateless). */
struct FSerialTraversePolicy
{
	template <typename... FCallables>
	void Run(FCallables&&... Callables) const
	{
		(Callables(), ...);
	}
};

/**
 * Scheduler contract — a state-free identity a concrete policy (e.g. the
 * engine's FParallelScheduler) derives from. It declares the traverse API; a
 * derived policy provides Run/RunTasks.
 */
class IScheduler
{
public:
	virtual ~IScheduler() = default;

	/** Drive every callable (serial / parallel — derived policy). */
	template <typename... FCallables>
	void Run(FCallables&&...) const = delete;
};

/**
 * Unpack a TTypeList, schedule per-type visits, and feed each type to Visitor
 * as a TTag<T>.
 *
 *   ForEach<FList>(FSerialTraversePolicy{}, [](auto Tag) {
 *       using T = typename decltype(Tag)::Type;
 *       // per-type work
 *   });
 */
template <typename TScheduler, typename TVisitor, typename... TArgs, typename... Ts>
void ForEachImpl(TTypeList<Ts...>, TScheduler&& Scheduler, TVisitor&& Visitor, TArgs&&... Args)
{
	Scheduler.Run([&] { Visitor(TTag<Ts>{}, Args...); }...);
}

template <typename TList, typename TScheduler, typename TVisitor, typename... TArgs>
	requires FForEachScheduler<TScheduler>
void ForEach(TScheduler&& Scheduler, TVisitor&& Visitor, TArgs&&... Args)
{
	ForEachImpl(TList{}, std::forward<TScheduler>(Scheduler), std::forward<TVisitor>(Visitor), std::forward<TArgs>(Args)...);
}

namespace Parallel
{

/**
 * Parallel scheduler — the parallel traverse base (thread pool).
 *
 * A FLayer drives its children through FLayer::Query().ForEach — that path is
 * built on this pool (RunTasks, barrier-per-level, parallel within a level).
 *
 * Dependency LEVELS and instance dispatch are the LAYER's concern (FLayer holds
 * FLevels + DispatchInstance inside Query().ForEach), not the scheduler's.
 */
class FParallelScheduler : public IScheduler
{
public:
	FParallelScheduler()
		: Pool(std::make_unique<FThreadPool>())
	{
	}

	/**
	 * Generic PARALLEL ForEach over a compile-time callable pack: every callable
	 * runs on the thread pool concurrently, returns when ALL complete (barrier).
	 * No type/layer/level semantics — the caller decides what each callable does.
	 *
	 *   Sched.ForEach([]{ A(); }, []{ B(); }, []{ C(); });   // A/B/C run in parallel
	 */
	template <typename... FCallables>
		requires (std::is_invocable_v<FCallables> && ...)
	void ForEach(FCallables&&... Callables) const
	{
		Pool->Run(std::forward<FCallables>(Callables)...);
	}

	/**
	 * Generic PARALLEL ForEach over a runtime container: each element is projected
	 * to a task via MakeTask and runs concurrently; returns when all complete.
	 * No type/layer/level semantics — any iterable range, MakeTask maps each
	 * element to its std::function<void()> work item. Constrained to containers so
	 * it disambiguates from the variadic callable overload.
	 *
	 *   std::vector<FWidget*> Widgets = ...;
	 *   Sched.ForEach(Widgets, [](FWidget* W) { return [W] { W->Draw(); }; });
	 */
	template <typename TContainer, typename TTaskFn>
		requires requires(const TContainer& c) { c.begin(); c.end(); c.size(); }
	void ForEach(const TContainer& Items, TTaskFn&& MakeTask) const
	{
		std::vector<std::function<void()>> Tasks;
		Tasks.reserve(Items.size());
		for (const auto& Item : Items)
		{
			Tasks.emplace_back(MakeTask(Item));
		}
		RunTasks(std::move(Tasks));
	}

private:
	/** Runtime task array — run on the thread pool (parallel, barrier at end). */
	void RunTasks(std::vector<std::function<void()>> Tasks) const
	{
		Pool->RunTasks(std::move(Tasks));
	}

	std::unique_ptr<FThreadPool> Pool;
};

namespace TTypeQueryDetail
{
	template <typename T, typename TSelectedList>
	struct TDerivesAny;

	template <typename T, typename THead, typename... TRest>
	struct TDerivesAny<T, TTypeList<THead, TRest...>>
		: std::disjunction<std::is_base_of<THead, T>, TDerivesAny<T, TTypeList<TRest...>>>
	{
	};

	template <typename T>
	struct TDerivesAny<T, TTypeList<>> : std::false_type
	{
	};

	/** keep the types in TList deriving at least one of TSelectedList */
	template <typename TList, typename TSelectedList>
	struct TFilter;

	template <typename TSelectedList, typename THead, typename... TRest>
	struct TFilter<TTypeList<THead, TRest...>, TSelectedList>
	{
		using FTAil = typename TFilter<TTypeList<TRest...>, TSelectedList>::Type;
		using Type = std::conditional_t<
			TDerivesAny<THead, TSelectedList>::value,
			typename TCatch<TTypeList<THead>, FTAil>::Type,
			FTAil>;
	};

	template <typename TSelectedList>
	struct TFilter<TTypeList<>, TSelectedList>
	{
		using Type = TTypeList<>;
	};
}

/**
 * Compile-time query over a TYPE table (e.g. a layer's FLayers). Select<T>()
 * keeps the types deriving T; ForEach drives each surviving type's singleton
 * (T::Get()) level-by-level in parallel — the sugar that lets singletons live
 * in the same plugin table as layer instances.
 *
 *   using FTable = TTypeList<FLog, FNet>;
 *   TypeQuery<FTable>().Select<ITick>().ForEach([](auto& S) { S.Tick(); });
 */
template <typename FList, typename... TSelected>
class TTypeQuery
{
public:
	template <typename... TInterfaces>
	[[nodiscard]] constexpr auto Select() const
	{
		return TTypeQuery<FList, TSelected..., TInterfaces...>{};
	}

	/** Drive each surviving type's singleton (T::Get()) level-by-level, parallel. */
	template <typename TVisitor>
	void ForEach(TVisitor&& Visitor) const
	{
		using FMatched = typename TTypeQueryDetail::TFilter<FList, TTypeList<TSelected...>>::Type;
		static_assert(FMatched::Count > 0,
			"TTypeQuery::ForEach: no types derive the selected interfaces");

		using FLevels = Topo::TLevels_t<FMatched, FDefaultSlot>;
		// layer semantics live HERE (a compile-time query over a type table) — the
		// scheduler only runs the already-assembled parallel task batches.
		Parallel::FParallelScheduler Sched;
		Maho::ForEach<FLevels>(Maho::FSerialTraversePolicy{}, [&](auto Tag) {
			using FLevel = typename decltype(Tag)::Type;
			std::vector<std::function<void()>> Tasks;
			Tasks.reserve(FLevel::Count);
			Maho::ForEach<FLevel>(Maho::FSerialTraversePolicy{}, [&](auto TypeTag) {
				using T = typename decltype(TypeTag)::Type;
				Tasks.emplace_back([&] { Visitor(T::Get()); });
			});
			// each task in this level -> a parallel work item, barrier after level.
			Sched.ForEach(Tasks, [](const std::function<void()>& T) { return T; });
		});
	}
};

/** Build a compile-time query over a type table. */
template <typename FList>
[[nodiscard]] constexpr auto TypeQuery()
{
	return TTypeQuery<FList>{};
}

} // namespace Parallel

} // namespace Maho
