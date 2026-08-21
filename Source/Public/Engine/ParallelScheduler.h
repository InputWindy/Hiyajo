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
 * Parallel scheduler — the parallel drive policy.
 *
 * Two Execute overloads — both parallel (per level), ordered by dependency
 * level:
 *   Execute<Stage, TList>(Visitor)          — compile-time: drives extension
 *     types, handing each a TTag<T> so the visitor calls T::Get().xxx().
 *   Execute<Stage>(vector<IAssembly*>&, Vis)— runtime: drives layer instances,
 *     handing each IAssembly* to the visitor. The visitor dispatches per build.
 *
 * Run = thread pool; between levels serial, within a level parallel.
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

	/** Compile-time drive: every T in TExtensions at Stage sees Visitor(TTag<T>, Stage). */
	template <auto Stage, typename TExtensions, typename TVisitor, typename TTopology = FForwardTopology>
	void Execute(TVisitor&& Visitor)
	{
		using FLevels = typename TTopology::template Apply<Topo::TLevels_t<TExtensions, Stage>>;
		ForEach<FLevels>(FSerialTraversePolicy{}, [&](auto LevelTag) {
			using FLevel = typename decltype(LevelTag)::Type;
			ForEach<FLevel>(*this, [&](auto Tag) {
				Visitor(Tag, Stage);
			});
		});
	}

	/** Runtime drive: every IAssembly* in Layers sees Visitor(instance, Stage). */
	template <auto Stage, typename TVisitor>
	void Execute(std::vector<IAssembly*>& Layers, TVisitor&& Visitor)
	{
		std::vector<std::function<void()>> Tasks;
		Tasks.reserve(Layers.size());
		for (IAssembly* Instance : Layers)
		{
			Tasks.emplace_back([&, Instance] { Visitor(Instance, Stage); });
		}
		Pool->RunTasks(std::move(Tasks));
	}

private:
	std::unique_ptr<FThreadPool> Pool;
};

} // namespace Parallel

} // namespace Maho
