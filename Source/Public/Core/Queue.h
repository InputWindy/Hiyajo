#pragma once

#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace Maho
{

/**
 * Thread-safe FIFO pending-VALUE command collection, partitioned by CATALOG.
 *
 * TCatalog is any less-than-comparable key (enum / integer); TCommand is any
 * plain value type. Each catalog owns its own FIFO, so Enqueue/Dequeue route by
 * category. It holds commands ONLY — execution is the consumer's job (the queue
 * never runs anything). No unique_ptr, no polymorphism, no execute protocol.
 *
 * TCommand must be:
 *   - copyable / moveable (stored by value)
 *   - comparable with operator== (dedupe, per catalog)
 *
 *   enum class ECmd { A, B };
 *   FQueue<ECmd, FMyCommand> Q;
 *   Q.Enqueue(ECmd::A, FMyCommand{...});        // dedupe within catalog A
 *   Q.Enqueue(ECmd::B, FMyCommand{...});        // separate catalog B FIFO
 *   while (auto C = Q.Dequeue(ECmd::A))         // drain catalog A, oldest first
 *   { /* consumer decides what "run" means */ }
 *
 * The consumer is the ENQUEUEing side: it reads via Dequeue and does the side
 * effect itself.
 */
template <typename TCatalog, typename TCommand>
class FQueue
{
public:
	/** Queue a command into its catalog — dedupe: identical pending values in the
	 *  same catalog are not queued twice. */
	void Enqueue(TCatalog Catalog, const TCommand& Cmd)
	{
		std::lock_guard Lock(Mutex);
		std::vector<TCommand>& Lane = List[Catalog];
		if (std::find(Lane.begin(), Lane.end(), Cmd) == Lane.end())
		{
			Lane.push_back(Cmd);
		}
	}

	/** Pop-and-return the OLDEST pending command in a catalog (FIFO). nullopt when
	 *  that catalog is empty. The consumer decides what "run" means. */
	[[nodiscard]] std::optional<TCommand> Dequeue(TCatalog Catalog)
	{
		std::lock_guard Lock(Mutex);
		auto It = List.find(Catalog);
		if (It == List.end() || It->second.empty())
		{
			return std::nullopt;
		}
		std::vector<TCommand>& Lane = It->second;
		TCommand Cmd = std::move(Lane.front());
		Lane.erase(Lane.begin());
		if (Lane.empty())
		{
			List.erase(It);
		}
		return Cmd;
	}

	/** True when nothing is pending in ANY catalog. */
	bool IsEmpty() const
	{
		std::lock_guard Lock(Mutex);
		return List.empty();
	}

	/** Number of pending commands across all catalogs. */
	std::size_t Size() const
	{
		std::lock_guard Lock(Mutex);
		std::size_t N = 0;
		for (const auto& [K, Lane] : List)
		{
			(void)K;
			N += Lane.size();
		}
		return N;
	}

private:
	mutable std::mutex Mutex;
	std::map<TCatalog, std::vector<TCommand>> List;
};

} // namespace Maho
