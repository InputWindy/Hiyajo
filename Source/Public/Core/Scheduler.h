#pragma once

#include <Core/Extension.h>
#include <Core/Topology.h>

#include <concepts>
#include <utility>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// The drive protocol: ExecuteExtension concept + ForEach traversal + the
// scheduler contract — all one family, all in this header.
//
//   concept FExtensionExecute     — "T has an ExecuteExtension<T>(Stage)"
//   concept FForEachScheduler     — "S has Run(Callables...)"
//   ForEach / TTag                — unpacks a TTypeList to per-type visits
//   IScheduler                    — Run + dual Execute (stage / lambda)
// ───────────────────────────────────────────────────────────────────────

// The per-extension execution hook: an extension is executed by a FREE
// function template, not a member — the host/plugin layer provides the
// overload/specialisation for each concrete (Extension, Stage) pair.
template <typename T, typename TStage>
concept FExtensionExecute = requires(TStage Stage)
{
	{ ExecuteExtension<T>(Stage) };
};

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

	/** Drive the extensions by stage (calls ExecuteExtension<T>(Stage) per level). */
	template <auto Stage, typename TExtensions, typename TTopology = FForwardTopology>
	void Execute() = delete;

	/** Drive the extensions by lambda (Visitor decides what to call — no stage). */
	template <typename TExtensions, typename TVisitor>
	void Execute(TVisitor&& Visitor) = delete;
};

} // namespace Maho
