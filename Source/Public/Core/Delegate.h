#pragma once

// Delegate -- multicast event building block (Core infrastructure, type-agnostic).
// TMulticastEvent<Signature>: bind handlers, broadcast values. Header-only, no
// state, no DLL boundary -- consumers include <Core/Delegate.h> and use it
// directly (a plugin's public API can expose it as a member type).
//
//   Maho::TMulticastEvent<void(const std::string&)> OnException;
//   auto Token = OnException.Bind([](const std::string& M) { ... });
//   OnException.Broadcast("boom");
//   OnException.Unbind(Token);   // remove only this subscription
//   OnException.RemoveAll();

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Maho
{

/** Opaque subscription id returned by TMulticastEvent::Bind, consumed by Unbind. */
using FSubscriptionID = uint64_t;

/** Minimal multicast event (bind + broadcast). Not thread-safe -- broadcast on
 *  the owning thread; use a queue to cross threads. */
template <typename Signature>
class TMulticastEvent;

template <typename... Args>
class TMulticastEvent<void(Args...)>
{
public:
	using FHandler = std::function<void(Args...)>;

	/** Register a handler and return a subscription id. The id (with Unbind) removes
	 *  ONLY this handler -- a subscriber owns its own subscription, so unsubscribing
	 *  one never affects others. Ids are never reused. */
	FSubscriptionID Bind(FHandler Handler)
	{
		const FSubscriptionID ID = NextID++;
		Handlers.push_back(FEntry{ID, std::move(Handler)});
		return ID;
	}

	/** Remove the handler registered under this subscription id. No-op if the id is
	 *  unknown (already unbound, or a stale id after RemoveAll). */
	void Unbind(FSubscriptionID ID)
	{
		Handlers.erase(std::remove_if(Handlers.begin(), Handlers.end(),
			[ID](const FEntry& E) { return E.ID == ID; }), Handlers.end());
	}

	void Broadcast(Args... Values) const
	{
		for (const auto& Entry : Handlers)
		{
			if (Entry.Handler)
			{
				Entry.Handler(Values...);
			}
		}
	}

	void RemoveAll()
	{
		Handlers.clear();
	}

private:
	struct FEntry
	{
		FSubscriptionID ID;
		FHandler Handler;
	};

	std::vector<FEntry> Handlers;
	FSubscriptionID NextID = 1;
};

} // namespace Maho
