#pragma once

#include <Engine/ThreadPool.h>
#include <Maho.h>

#include <memory>
#include <functional>
#include <vector>

namespace Maho
{

namespace Parallel
{

/**
 * Parallel scheduler — the parallel traverse base.
 *
 * Two Execute overloads, matching the two extension kinds:
 *
 *   Execute<TQueryTypes>(visitor)          — singletons: each type's T::Get() is
 *     handed to the visitor as T&. (Tools / type providers.)
 *   Execute<TQueryTypes>(Instances, vis)   — instances: for every IAssembly* in
 *     the array, its RUNTIME type is checked against TQueryTypes (the Query's
 *     filtered type list); the first matching type hands the typed instance
 *     (T&) to the visitor, others are skipped. (Layers.)
 *
 * Dependency LEVELS are not a scheduler concern: iterate Topo::TLevels_t with
 * a serial ForEach and call Execute per level (barrier between, parallel
 * within) — the host owns phasing.
 */
class FParallelScheduler : public IScheduler
{
public:
	FParallelScheduler()
		: Pool(std::make_unique<FThreadPool>())
	{
	}

	template <typename... FCallables>
	void Run(FCallables&&... Callables) const
	{
		Pool->Run(std::forward<FCallables>(Callables)...);
	}

	/** Singleton traverse: every T in TQueryTypes sees Visitor(T::Get()&). */
	template <typename TQueryTypes, typename TVisitor>
	void Execute(TVisitor&& Visitor)
	{
		ForEach<TQueryTypes>(*this, [&](auto Tag) {
			using T = typename decltype(Tag)::Type;
			Visitor(T::Get());
		});
	}

	/**
	 * Instance traverse: every IAssembly* in Instances whose runtime type
	 * matches one of TQueryTypes is handed to the visitor as that type (T&);
	 * non-matching instances are skipped. Each instance dispatches at most once
	 * (first matching type wins — order TQueryTypes from most to least derived).
	 */
	template <typename TQueryTypes, typename TVisitor>
	void Execute(std::vector<IAssembly*>& Instances, TVisitor&& Visitor)
	{
		std::vector<std::function<void()>> Tasks;
		Tasks.reserve(Instances.size());
		for (IAssembly* Instance : Instances)
		{
			Tasks.emplace_back([&, Instance] { DispatchInstance<TQueryTypes>(Instance, Visitor); });
		}
		Pool->RunTasks(std::move(Tasks));
	}

private:
	std::unique_ptr<FThreadPool> Pool;
};

} // namespace Parallel

} // namespace Maho
