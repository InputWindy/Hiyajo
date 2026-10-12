#pragma once


#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace Maho
{

using FSubscriptionID = uint64_t;

template <typename Signature>
class TMulticastEvent;

template <typename... Args>
class TMulticastEvent<void(Args...)>
{
public:
	using FHandler = std::function<void(Args...)>;

	FSubscriptionID Bind(FHandler Handler)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		const FSubscriptionID ID = NextID++;
		Handlers.push_back(FEntry{ID, std::move(Handler)});
		return ID;
	}

	void Unbind(FSubscriptionID ID)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Handlers.erase(std::remove_if(Handlers.begin(), Handlers.end(),
			[ID](const FEntry& E) { return E.ID == ID; }), Handlers.end());
	}

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

}
