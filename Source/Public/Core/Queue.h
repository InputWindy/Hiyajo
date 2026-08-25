#pragma once

#include <algorithm>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace Maho
{

struct ICommand
{
    virtual ~ICommand() = default;
    virtual void Execute() = 0;
};
    
/**
 * Thread-safe pending command collection over VALUE commands.
 *
 * TCommand is a plain value type — the queue stores copies, deduplicates, and
 * Flush() executes-and-clears them. No unique_ptr, no lifetime management, no
 * polymorphic identity: a command is whatever the enqueueing site needs to run
 * once at the next safe point.
 *
 * TCommand must be:
 *   - copyable (stored by value)
 *   - comparable with operator== (dedupe)
 *   - executable via Execute()
 *
 *   FQueue<FMyCommand> Q;
 *   Q.Enqueue(FMyCommand{...});  // dedupe: same value not queued twice
 *   Q.Dequeue(FMyCommand{...});  // dedupe: remove a pending command
 *   Q.Flush();                   // safe point: run all pending, then clear
 *
 * The consumer is the ENQUEUEing side (a layer, a service): it decides what a
 * command means when its Execute() runs at Flush.
 */
template <typename TCommand>
class FQueue
{
public:
	/** Queue a command — dedupe: identical pending values are not queued twice. */
	void Enqueue(const TCommand& Cmd)
	{
		std::lock_guard Lock(Mutex);
		if (std::find(Pending.begin(), Pending.end(), Cmd) == Pending.end())
		{
			Pending.push_back(Cmd);
		}
	}

	/** Remove a pending command (no-op if not queued). */
	void Dequeue(const TCommand& Cmd)
	{
		std::lock_guard Lock(Mutex);
		Pending.erase(std::remove(Pending.begin(), Pending.end(), Cmd), Pending.end());
	}

	/**
	 * Execute-and-clear: run up to MaxCommands pending commands, then drop them
	 * from the queue. MaxCommands == 0 (default) means ALL pending commands run
	 * and the queue empties; a smaller value lets a consumer pace consumption —
	 * the rest stay pending for the next Flush.
	 */
	void Flush(std::size_t MaxCommands = 0)
	{
		std::vector<TCommand> Commands;
		{
			std::lock_guard Lock(Mutex);
			const std::size_t Count = (MaxCommands == 0 || MaxCommands >= Pending.size())
				? Pending.size()
				: MaxCommands;
			Commands.reserve(Count);
			for (std::size_t i = 0; i < Count; ++i)
			{
				Commands.push_back(std::move(Pending[i]));
			}
			Pending.erase(Pending.begin(), Pending.begin() + Count);
		}
		for (TCommand& Cmd : Commands)
		{
			Cmd.Execute();
		}
	}

	/** True when nothing is pending. */
	bool IsEmpty() const
	{
		std::lock_guard Lock(Mutex);
		return Pending.empty();
	}

	/** Number of pending commands. */
	std::size_t Size() const
	{
		std::lock_guard Lock(Mutex);
		return Pending.size();
	}

protected:
	/** The pending set (latest snapshot) — for derived inspection. */
	const std::vector<TCommand>& Get() const
	{
		return Pending;
	}

	/** Drop all pending commands without executing them. */
	void ClearPending()
	{
		std::lock_guard Lock(Mutex);
		Pending.clear();
	}

private:
	mutable std::mutex Mutex;
	std::vector<TCommand> Pending;
};

} // namespace Maho
