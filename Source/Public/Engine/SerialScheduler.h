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

	/** Compile-time traverse: every T in TList sees Visitor(TTag<T>). */
	template <typename TList, typename TVisitor>
	void Execute(TVisitor&& Visitor)
	{
		ForEach<TList>(*this, std::forward<TVisitor>(Visitor));
	}
};

} // namespace Serial

} // namespace Maho
