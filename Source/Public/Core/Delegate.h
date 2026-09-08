#pragma once

// Delegate -- multicast event building block (Core infrastructure, type-agnostic).
// TMulticastEvent<Signature>: bind handlers, broadcast values. Header-only, no
// state, no DLL boundary -- consumers include <Core/Delegate.h> and use it
// directly (a plugin's public API can expose it as a member type).
//
// Thread-safe: Bind / Unbind / Broadcast / RemoveAll may be called from any
// thread. Broadcast snapshots the handlers under the lock, then invokes them
// OUTSIDE it, so a handler may re-enter the event (Bind/Unbind) safely.
//
//   Maho::TMulticastEvent<void(const std::string&)> OnException;
//   auto Token = OnException.Bind([](const std::string& M) { ... });
//   OnException.Broadcast("boom");
//   OnException.Unbind(Token);   // remove only this subscription
//   OnException.RemoveAll();

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace Maho
{

/** Opaque subscription id returned by TMulticastEvent::Bind, consumed by Unbind. */
using FSubscriptionID = uint64_t;

/** Thread-safe multicast event (bind + broadcast). Any thread may bind /
 *  unbind / broadcast. Broadcast copies the handler set under the lock and
 *  invokes the copy outside it, so handlers can re-enter the event safely. */
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
		std::lock_guard<std::mutex> Lock(Mutex);
		const FSubscriptionID ID = NextID++;
		Handlers.push_back(FEntry{ID, std::move(Handler)});
		return ID;
	}

	/** Remove the handler registered under this subscription id. No-op if the id is
	 *  unknown (already unbound, or a stale id after RemoveAll). */
	void Unbind(FSubscriptionID ID)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Handlers.erase(std::remove_if(Handlers.begin(), Handlers.end(),
			[ID](const FEntry& E) { return E.ID == ID; }), Handlers.end());
	}

	void Broadcast(Args... Values) const
	{
		// Snapshot under the lock, then invoke OUTSIDE it so a handler may safely
		// re-enter the event (Bind/Unbind/RemoveAll) without deadlocking.
		std::vector<FEntry> Copy;
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			Copy = Handlers;
		}
		for (const auto& Entry : Copy)
		{
			if (Entry.Handler)
			{
				Entry.Handler(Values...);
			}
		}
	}

	void RemoveAll()
	{
		std::lock_guard<std::mutex> Lock(Mutex);
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
	mutable std::mutex Mutex;
};

} // namespace Maho
