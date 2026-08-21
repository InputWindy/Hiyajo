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
 * lifecycle by calling Execute once per phase (init / per-frame tick / ...),
 * passing a visitor that decides what each target does:
 *
 *   Execute<FTools>(visitor)              — compile-time: parallel over the type
 *     list, each type T handed to the visitor as TTag<T> → T::Get().xxx().
 *   Execute(Layers, visitor)              — runtime: parallel over the instance
 *     vector, each IAssembly* handed to the visitor.
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

	/** Compile-time traverse: every T in TList sees Visitor(TTag<T>). */
	template <typename TList, typename TVisitor>
	void Execute(TVisitor&& Visitor)
	{
		ForEach<TList>(*this, std::forward<TVisitor>(Visitor));
	}

	/** Runtime traverse: every IAssembly* in Layers sees Visitor(instance). */
	template <typename TVisitor>
	void Execute(std::vector<IAssembly*>& Layers, TVisitor&& Visitor)
	{
		std::vector<std::function<void()>> Tasks;
		Tasks.reserve(Layers.size());
		for (IAssembly* Instance : Layers)
		{
			Tasks.emplace_back([&, Instance] { Visitor(Instance); });
		}
		Pool->RunTasks(std::move(Tasks));
	}

private:
	std::unique_ptr<FThreadPool> Pool;
};

} // namespace Parallel

} // namespace Maho
