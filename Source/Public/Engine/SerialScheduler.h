#pragma once

#include <Maho.h>

namespace Maho
{

namespace Serial
{

/**
 * Serial scheduler — the serial traverse base. Execute has no stage semantics;
 * it is a plain traverse (compile-time type list / runtime instance vector).
 */
class FSerialScheduler : public IScheduler
{
public:
	template <typename... FCallables>
	void Run(FCallables&&... Callables) const
	{
		(Callables(), ...);
	}

	/** Compile-time traverse: every tool singleton in TList sees Visitor(T&). */
	template <typename TList, typename TVisitor>
	void Execute(TVisitor&& Visitor)
	{
		ForEach<TList>(*this, [&](auto Tag) {
			using T = typename decltype(Tag)::Type;
			Visitor(T::Get());
		});
	}
};

} // namespace Serial

} // namespace Maho
