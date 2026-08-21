#pragma once

#include <Maho.h>

namespace Maho
{

namespace Serial
{

/**
 * Serial scheduler — the serial drive policy.
 *
 * Compile-time Execute<Stage, TList>(Visitor): drives extension types,
 * handing each a TTag<T> + Stage so the visitor calls capabilities. Main and
 * the stage mapping belong to the host, not the policy.
 */
class FSerialScheduler : public IScheduler
{
public:
	template <typename... FCallables>
	void Run(FCallables&&... Callables) const
	{
		(Callables(), ...);
	}

	template <auto Stage, typename TExtensions, typename TVisitor, typename TTopology = FForwardTopology>
	void Execute(TVisitor&& Visitor)
	{
		using FLevels = typename TTopology::template Apply<Topo::TLevels_t<TExtensions, Stage>>;
		ForEach<FLevels>(FSerialTraversePolicy{}, [&](auto LevelTag) {
			using FLevel = typename decltype(LevelTag)::Type;
			ForEach<FLevel>(FSerialTraversePolicy{}, [&](auto Tag) {
				Visitor(Tag, Stage);
			});
		});
	}
};

} // namespace Serial

} // namespace Maho
