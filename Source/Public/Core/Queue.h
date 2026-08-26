#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace Maho
{

/**
 * The runtime base every queued command derives from. Type-erased: FQueue holds
 * these via unique_ptr, so the queue is type-agnostic. GetCatalogId() is the
 * catalog key that routes the command into its own FIFO lane. A command is a
 * DATA carrier — execution is entirely the consumer's job (the queue never runs
 * anything).
 */
struct ICommand
{
	virtual ~ICommand() = default;

	/** The catalog lane this command belongs to (uint64 key). */
	[[nodiscard]] virtual std::uint64_t GetCatalogId() const = 0;
};

/**
 * Thread-safe FIFO pending-command collection, partitioned by CATALOG.
 *
 * Type-agnostic: commands are held as unique_ptr<ICommand>; each command carries
 * its own catalog id (GetCatalogId), so Enqueue routes it into the right FIFO
 * lane and Dequeue drains one lane. The queue only HOLDS commands — the consumer
 * Dequeues and applies them itself (e.g. switch on GetCatalogId).
 *
 *   FQueue Q;
 *   Q.Enqueue(std::make_unique<FInstallCmd>(...));   // routed by GetCatalogId
 *   while (auto Cmd = Q.Dequeue(kInstallLane))       // drain that lane, FIFO
 *   { ... }                                          // apply each command yourself
 *
 * Any thread may Enqueue; the consumer Dequeues at its own safe point.
 */
class FQueue
{
public:
	/** Queue a command into its catalog lane (takes ownership). */
	void Enqueue(std::unique_ptr<ICommand> Cmd)
	{
		if (!Cmd)
		{
			return;
		}
		std::lock_guard Lock(Mutex);
		List[Cmd->GetCatalogId()].push_back(std::move(Cmd));
	}

	/** Pop-and-return the OLDEST pending command in a catalog lane (FIFO).
	 *  nullptr when that lane is empty. Ownership transfers to the caller. */
	[[nodiscard]] std::unique_ptr<ICommand> Dequeue(std::uint64_t CatalogId)
	{
		std::lock_guard Lock(Mutex);
		auto It = List.find(CatalogId);
		if (It == List.end() || It->second.empty())
		{
			return nullptr;
		}
		std::unique_ptr<ICommand> Cmd = std::move(It->second.front());
		It->second.erase(It->second.begin());
		if (It->second.empty())
		{
			List.erase(It);
		}
		return Cmd;
	}

	/** True when nothing is pending in ANY catalog lane. */
	bool IsEmpty() const
	{
		std::lock_guard Lock(Mutex);
		return List.empty();
	}

	/** Number of pending commands across all catalog lanes. */
	std::size_t Size() const
	{
		std::lock_guard Lock(Mutex);
		std::size_t N = 0;
		for (const auto& [Lane, Items] : List)
		{
			(void)Lane;
			N += Items.size();
		}
		return N;
	}

private:
	mutable std::mutex Mutex;
	std::map<std::uint64_t, std::vector<std::unique_ptr<ICommand>>> List;
};

} // namespace Maho
