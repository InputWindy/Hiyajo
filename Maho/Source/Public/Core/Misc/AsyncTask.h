#pragma once

#include <Core/Misc/Export.h>

#include <functional>
#include <memory>

namespace Maho
{

/**
 * One-shot async job on a temporary std::thread (not the persistent FWorkerPool).
 * Launch starts the thread; Wait joins it. Destructor Wait()s if still running.
 *
 * Example:
 * ```
 *   auto Task = Maho::FAsyncTask::LaunchNew([]()
 *   {
 *       // runs on a dedicated temporary thread
 *   });
 *   Task->Wait();
 *
 *   // Or stack-owned:
 *   Maho::FAsyncTask Local;
 *   Local.Launch([]() { ... });
 *   Local.Wait();
 * ```
 */
class MAHO_API FAsyncTask
{
public:
	FAsyncTask();
	~FAsyncTask();

	FAsyncTask(const FAsyncTask&) = delete;
	FAsyncTask& operator=(const FAsyncTask&) = delete;

	FAsyncTask(FAsyncTask&& Other) noexcept;
	FAsyncTask& operator=(FAsyncTask&& Other) noexcept;

	/**
	 * Start Work on a new thread. If a previous launch is still running, Wait()s first.
	 * No-op if Work is empty.
	 */
	void Launch(std::function<void()> Work);

	/** Block until the launched work finishes (and join the thread). */
	void Wait();

	/** True after work finished (and Wait may or may not have been called yet). */
	[[nodiscard]] bool IsDone() const;

	/** True while a thread was launched and has not been Wait()/joined yet. */
	[[nodiscard]] bool IsRunning() const;

	/** Heap helper: create + Launch in one call. */
	[[nodiscard]] static std::unique_ptr<FAsyncTask> LaunchNew(std::function<void()> Work);

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

} // namespace Maho
