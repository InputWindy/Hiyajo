#pragma once

// Delegate — multicast event building block (Core infrastructure, type-agnostic).
// TMulticastEvent<Signature>: bind handlers, broadcast values. Header-only, no
// state, no DLL boundary — consumers include <Core/Delegate.h> and use it
// directly (a plugin's public API can expose it as a member type).
//
//   Maho::TMulticastEvent<void(const std::string&)> OnException;
//   OnException.Bind([](const std::string& M) { ... });
//   OnException.Broadcast("boom");
//   OnException.RemoveAll();

#include <functional>
#include <utility>
#include <vector>

namespace Maho
{

/** Minimal multicast event (bind + broadcast). Not thread-safe — broadcast on
 *  the owning thread; use a queue to cross threads. */
template <typename Signature>
class TMulticastEvent;

template <typename... Args>
class TMulticastEvent<void(Args...)>
{
public:
	using FHandler = std::function<void(Args...)>;

	void Bind(FHandler Handler)
	{
		Handlers.push_back(std::move(Handler));
	}

	void Broadcast(Args... Values) const
	{
		for (const auto& Handler : Handlers)
		{
			if (Handler)
			{
				Handler(Values...);
			}
		}
	}

	void RemoveAll()
	{
		Handlers.clear();
	}

private:
	std::vector<FHandler> Handlers;
};

} // namespace Maho
