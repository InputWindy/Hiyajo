#pragma once

#include <Maho.h>

#include <functional>
#include <vector>

namespace Maho
{

namespace Serial
{

/**
 * Serial scheduler — the serial traverse base (no threads). Mirrors the
 * parallel scheduler's API so the two are interchangeable:
 *   Execute<TQueryTypes>(visitor)          — singletons (T::Get()).
 *   Execute<TQueryTypes>(Instances, vis)   — instances (dispatch by runtime type).
 *   ExecuteLevels<TLevels>(Instances, vis) — instances by static dependency levels.
 */
class FSerialScheduler : public IScheduler
{
public:
	template <typename... FCallables>
	void Run(FCallables&&... Callables) const
	{
		(Callables(), ...);
	}

	/** Runtime task array — run in order (serial). */
	void RunTasks(std::vector<std::function<void()>> Tasks) const
	{
		for (auto& Task : Tasks)
		{
			Task();
		}
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

	/** Instance traverse: first matching type in TQueryTypes sees Visitor(T&). */
	template <typename TQueryTypes, typename TVisitor>
	void Execute(std::vector<IAssembly*>& Instances, TVisitor&& Visitor)
	{
		for (IAssembly* Instance : Instances)
		{
			DispatchInstance<TQueryTypes>(Instance, Visitor);
		}
	}

	/** Drive instances by static dependency levels (serial within and across). */
	template <typename TLevels, typename TVisitor>
	void ExecuteLevels(std::vector<IAssembly*>& Instances, TVisitor&& Visitor)
	{
		ForEach<TLevels>(FSerialTraversePolicy{}, [&](auto LevelTag) {
			using FLevel = typename decltype(LevelTag)::Type;
			Execute<FLevel>(Instances, Visitor);
		});
	}
};

} // namespace Serial

} // namespace Maho
