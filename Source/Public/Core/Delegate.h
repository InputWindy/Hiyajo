/// @file
/// @brief Multicast events: `TMulticastEvent<void(Args...)>` -- bind / broadcast / unbind, thread-safe,
///        usable across a DLL boundary.
///
/// A subscriber gets an opaque `FSubscriptionID` back and unbinds with it, so a listener that dies
/// before its publisher can always detach (this is what makes "a service broadcasts to plugins" safe
/// when a plugin is unloaded). `Broadcast` copies the handler list under the lock and then calls
/// WITHOUT it: a handler may bind/unbind (or broadcast) re-entrantly, and holding the lock across the
/// calls would deadlock on the first such handler.
///
/// The event itself never crosses a DLL boundary as an interface: a publisher owns it and hands out
/// subscription ids.
#pragma once


#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace Maho
{

/// @brief One subscriber's handle, returned by `Bind` and passed back to `Unbind`.
using FSubscriptionID = uint64_t;

/// @brief The event's interface, specialised on a function signature.
/// @tparam Signature The broadcast signature, e.g. `void(const Name::FName&, int)`.
template <typename Signature>
class TMulticastEvent;

/// @brief A multicast event with signature `void(Args...)`.
/// @tparam Args What a broadcast passes to every handler.
template <typename... Args>
class TMulticastEvent<void(Args...)>
{
public:
	/// @brief What a subscriber registers: any callable taking `Args...`.
	using FHandler = std::function<void(Args...)>;

	/// @brief Subscribes a handler.
	/// @param Handler The callable to invoke on every broadcast (an empty one is skipped).
	/// @return The subscription id, to be passed back to `Unbind`.
	FSubscriptionID Bind(FHandler Handler)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		const FSubscriptionID ID = NextID++;
		Handlers.push_back(FEntry{ID, std::move(Handler)});
		return ID;
	}

	/// @brief Detaches a subscription. Idempotent: an unknown id is ignored.
	/// @param ID The id `Bind` returned.
	void Unbind(FSubscriptionID ID)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Handlers.erase(std::remove_if(Handlers.begin(), Handlers.end(),
			[ID](const FEntry& E) { return E.ID == ID; }), Handlers.end());
	}

	/// @brief Invokes every currently-bound handler, in bind order.
	///
	/// The list is copied under the lock and the calls happen outside it, so a handler may bind, unbind
	/// or broadcast again -- and a handler bound DURING this broadcast does not receive it (it sees the
	/// next one). That is the contract callers rely on when they tear something down from a callback.
	/// @param Values What to pass to each handler.
	void Broadcast(Args... Values) const
	{
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

	/// @brief Detaches every subscription (a publisher tearing down its own event).
	void RemoveAll()
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Handlers.clear();
	}

private:
	/// @brief One bound handler, with the id it is known by.
	struct FEntry
	{
		/// @brief The id `Bind` returned for this handler.
		FSubscriptionID ID;

		/// @brief The callable itself (an empty one is skipped by `Broadcast`).
		FHandler Handler;
	};

	/// @brief The bound handlers, in bind order.
	std::vector<FEntry> Handlers;

	/// @brief The next id to hand out (starts at 1: 0 is never a valid subscription).
	FSubscriptionID NextID = 1;

	/// @brief Guards `Handlers` and `NextID`. Mutable so a const `Broadcast` can take it.
	mutable std::mutex Mutex;
};

}
