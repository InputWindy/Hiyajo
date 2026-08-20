#pragma once

#include <Maho.h>

namespace Maho
{

namespace Serial
{

/**
 * Serial scheduler — the serial drive policy.
 *
 * Only Run (serial fold) + Execute (level-by-level serial drive). Main and
 * ExecuteStage belong to the host, not the policy.
 */
class FSerialScheduler : public IScheduler
{
public:
	template <typename... FCallables>
	void Run(FCallables&&... Callables) const
	{
		(Callables(), ...);
	}

	template <auto Stage, typename TExtensions, typename TTopology = FForwardTopology>
	void Execute()
	{
		using FLevels = typename TTopology::template Apply<Topo::TLevels_t<TExtensions, Stage>>;
		ForEach<FLevels>(FSerialTraversePolicy{}, [](auto LevelTag) {
			using FLevel = typename decltype(LevelTag)::Type;
			ForEach<FLevel>(FSerialTraversePolicy{}, [](auto Tag) {
				using T = typename decltype(Tag)::Type;
				static_assert(FExtensionExecute<T, decltype(Stage)>,
					"Extension must provide ExecuteExtension<T, Stage>(Stage)");
				ExecuteExtension<T, decltype(Stage)>(Stage);
			});
		});
	}
};

} // namespace Serial

} // namespace Maho
