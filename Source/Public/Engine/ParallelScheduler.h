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
 * Execute is a parallel base ONLY — no stage semantics. The host expresses
 * lifecycle by calling Execute once per phase, passing a visitor that decides
 * what each target does. Two flavors, matching the two extension kinds:
 *
 *   Execute<TQueryTypes>(visitor)          — singletons: each type's T::Get() is
 *     handed to the visitor as T&. (Tools / type providers.)
 *   Execute<TQueryTypes>(Instances, vis)   — instances: for every IAssembly* in
 *     the array, its RUNTIME type is checked against TQueryTypes (the Query's
 *     filtered type list); the first matching type hands the typed instance
 *     (T&) to the visitor, others are skipped. (Layers.)
 *   ExecuteLevels<TLevels>(Instances, vis) — drive the instances by the static
 *     dependency levels: each level (a TTypeList of peer types) runs its
 *     matching instances in parallel; levels are serialized (dependency
 *     barriers come from Topo::TLevels_t).
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

	/**
	 * Drive instances by static dependency levels. TLevels is the output of
	 * Topo::TLevels_t<TQueryTypes, Key>: TTypeList<TTypeList<L0...>, TTypeList<L1...>...>.
	 * Levels run serially (dependency barrier); within a level, matching
	 * instances run in parallel.
	 */
	template <typename TLevels, typename TVisitor>
	void ExecuteLevels(std::vector<IAssembly*>& Instances, TVisitor&& Visitor)
	{
		ForEach<TLevels>(FSerialTraversePolicy{}, [&](auto LevelTag) {
			using FLevel = typename decltype(LevelTag)::Type;
			Execute<FLevel>(Instances, Visitor);
		});
	}

private:
	std::unique_ptr<FThreadPool> Pool;
};

} // namespace Parallel

} // namespace Maho
