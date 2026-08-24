#pragma once

#include <Core/TypeList.h>
#include <Engine/ThreadPool.h>

#include <concepts>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Maho
{

// FLayerBase lives in Engine/Layer.h — forward-declared here so the runtime
// dispatch can hold pointers to it (only the name is needed at this layer).
class FLayerBase;

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

// ───────────────────────────────────────────────────────────────────────
// Runtime instance dispatch.
//
// A container of FLayerBase* holds polymorphic instances whose RUNTIME type is
// known only at runtime. DispatchInstance<TList>(instance, visitor) dynamic_cast
// to the first type in TList the instance binds to (order matters — list most
// derived first) and calls visitor(T&). Each instance is driven at most once;
// instances matching no candidate are skipped.
// ───────────────────────────────────────────────────────────────────────
namespace InstanceDispatchDetail
{
	template <typename THead, typename... TRest, typename TVisitor>
	bool TCall(FLayerBase* Instance, TVisitor& Visitor)
	{
		if (auto* Typed = dynamic_cast<THead*>(Instance))
		{
			Visitor(*Typed);
			return true;
		}
		if constexpr (sizeof...(TRest) > 0)
		{
			return TCall<TRest...>(Instance, Visitor);
		}
		return false;
	}
}

/** Drive Instance once: first matching type in TList sees Visitor(T&). */
template <typename TList, typename TVisitor>
void DispatchInstance(FLayerBase* Instance, TVisitor& Visitor);

template <typename... Ts, typename TVisitor>
void DispatchInstance(TTypeList<Ts...>, FLayerBase* Instance, TVisitor& Visitor)
{
	if (Instance == nullptr)
	{
		return;
	}
	InstanceDispatchDetail::TCall<Ts...>(Instance, Visitor);
}

/** Drive Instance once: first matching type in TList sees Visitor(T&). */
template <typename TList, typename TVisitor>
void DispatchInstance(FLayerBase* Instance, TVisitor& Visitor)
{
	DispatchInstance(TList{}, Instance, Visitor);
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
template <typename FExtensions = TTypeList<>>
class FParallelScheduler : public IScheduler
{
public:
	using FExtensionList = FExtensions;

	FParallelScheduler()
		: Pool(std::make_unique<FThreadPool>())
	{
	}

	template <typename... FCallables>
	void Run(FCallables&&... Callables) const
	{
		Pool->Run(std::forward<FCallables>(Callables)...);
	}

	/** Runtime task array — run on the thread pool (parallel, barrier at end). */
	void RunTasks(std::vector<std::function<void()>> Tasks) const
	{
		Pool->RunTasks(std::move(Tasks));
	}

private:
	std::unique_ptr<FThreadPool> Pool;
};

} // namespace Parallel

} // namespace Maho
