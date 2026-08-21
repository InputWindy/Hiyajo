#pragma once

#include <Core/Assembly.h>
#include <Core/Extension.h>
#include <Core/Topology.h>

#include <concepts>
#include <utility>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// The drive protocol: the traversal machinery + the scheduler contract.
//
//   forall FForEachScheduler   — "S has Run(Callables...)"
//   ForEach / TTag             — unpacks a TTypeList to per-type visits
//   IScheduler                 — Run + Execute (stage drive)
//
// There is NO extension-interaction protocol beside the visitor lambda: the
// scheduler only traverses lists (compile-time) or instances (runtime) and
// hands each target to a visitor the host passes in. Extensions expose only
// capability methods; the host decides what each stage does in the lambda.
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
 * Unpack a TTypeList, schedule per-type visits, and feed each type to Visitor
 * as a TTag<T>.
 *
 *   ForEach<FList>(FSerialScheduler{}, [](auto Tag) {
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

/**
 * Scheduler base — declares the Run + Execute contracts.
 *
 * Run    — drives a set of callables (serial / parallel — derived policy).
 * Execute — drives a group of extensions by dependency levels: within a level
 *           the extensions run by the derived policy, between levels serially.
 */
class IScheduler
{
public:
	virtual ~IScheduler() = default;

	/** Drive every callable (serial / parallel — derived policy). */
	template <typename... FCallables>
	void Run(FCallables&&... Callables) const = delete;

	/** (See the derived policy for the two Execute overloads.) */
};

// ───────────────────────────────────────────────────────────────────────
// Runtime instance dispatch (shared by the serial & parallel schedulers).
//
// A std::vector<IAssembly*> holds polymorphic instances whose RUNTIME type is
// known only at runtime. DispatchInstance<TList>(instance, visitor) dynamic_cast
// to the first type in TList the instance binds to (order matters — list most
// derived first) and calls visitor(T&). Each instance is driven at most once;
// instances matching no candidate are skipped.
// ───────────────────────────────────────────────────────────────────────
namespace InstanceDispatchDetail
{
	template <typename THead, typename... TRest, typename TVisitor>
	bool TCall(IAssembly* Instance, TVisitor& Visitor)
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
void DispatchInstance(IAssembly* Instance, TVisitor& Visitor);

template <typename... Ts, typename TVisitor>
void DispatchInstance(TTypeList<Ts...>, IAssembly* Instance, TVisitor& Visitor)
{
	if (Instance == nullptr)
	{
		return;
	}
	InstanceDispatchDetail::TCall<Ts...>(Instance, Visitor);
}

/** Drive Instance once: first matching type in TList sees Visitor(T&). */
template <typename TList, typename TVisitor>
void DispatchInstance(IAssembly* Instance, TVisitor& Visitor)
{
	DispatchInstance(TList{}, Instance, Visitor);
}

} // namespace Maho
