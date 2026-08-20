#pragma once

#include <Engine/ThreadPool.h>
#include <Maho.h>

#include <memory>

namespace Maho
{

namespace Parallel
{

/**
 * Parallel scheduler — the parallel drive policy.
 *
 * Only Run (thread pool) + Execute (level-by-level parallel drive). Main,
 * the tick loop and shutdown belong to the host, not the policy.
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

	template <auto Stage, typename TExtensions, typename TTopology = FForwardTopology>
	void Execute()
	{
		using FLevels = typename TTopology::template Apply<Topo::TLevels_t<TExtensions, Stage>>;
		ForEach<FLevels>(FSerialTraversePolicy{}, [&](auto LevelTag) {
			using FLevel = typename decltype(LevelTag)::Type;
			ForEach<FLevel>(*this, [](auto Tag) {
				using T = typename decltype(Tag)::Type;
				static_assert(FExtensionExecute<T, decltype(Stage)>,
					"Extension must provide ExecuteExtension<T>(Stage)");
				ExecuteExtension<T>(Stage);
			});
		});
	}

	template <typename TExtensions, typename TVisitor>
	void Execute(TVisitor&& Visitor)
	{
		using FLevels = typename FForwardTopology::template Apply<Topo::TLevels_t<TExtensions, FDefaultSlot>>;
		ForEach<FLevels>(FSerialTraversePolicy{}, [&](auto LevelTag) {
			using FLevel = typename decltype(LevelTag)::Type;
			ForEach<FLevel>(*this, Visitor);
		});
	}

private:
	std::unique_ptr<FThreadPool> Pool;
};

} // namespace Parallel

} // namespace Maho
