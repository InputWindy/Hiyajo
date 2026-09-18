#pragma once

// ThreadedServer -- a dedicated resident worker: one persistent thread + FIFO
// task queue (not in Engine/Common: it's engine infrastructure alongside
// FThreadPool, not a service). Use for long-lived ROLES (render thread, IO load
// thread, ...) that need a private always-on thread with a serial command
// queue -- NOT for transient parallel work (use FThreadPool).
//
//   class FResourceSystem : public TSingleton<FResourceSystem>
//                          , public FThreadedServer { ... };
//   FResourceSystem::Get().Initialize();   // start the dedicated thread
//   FResourceSystem::Get().Submit([...]{ /* runs on the worker */ });
//   FResourceSystem::Get().Flush();        // barrier: drain everything before
//   FResourceSystem::Get().Shutdown();     // stop + join
//
// The implementation lives in Source/Private/Core/ThreadedServer.cpp -- including
// the virtuals, so the class has a KEY FUNCTION in Maho.dll rather than a vtable
// copy per module (see Core/Export.h).

#include <Core/Export.h>
#include <Core/Profiler.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace Maho
{

/**
 * Dedicated resident worker. Subclass + override OnInitialize / OnShutdown /
 * GetThreadName for role-specific setup (e.g. the resource system resolves its
 * roots in OnInitialize). Flush() is a FIFO barrier.
 */
class MAHO_API FThreadedServer
{
public:
	FThreadedServer() = default;
	virtual ~FThreadedServer();

	FThreadedServer(const FThreadedServer&) = delete;
	FThreadedServer& operator=(const FThreadedServer&) = delete;

	/** Start the dedicated worker; idempotent. Returns false on OnInitialize failure. */
	bool Initialize();

	/** Stop + join the worker; idempotent. */
	void Shutdown();

	[[nodiscard]] bool IsRunning() const
	{
		return bRunning.load(std::memory_order_acquire);
	}

	/** Enqueue one task (non-blocking, FIFO, serial execution). Traced as "<role>::Task". */
	void Submit(std::function<void()> Task);

	/** Enqueue a task that names itself. The trace bar is opened where the task RUNS (the server
	 *  thread), and its label is "<role>::<Stage>" -- the role comes from GetThreadName, so the
	 *  caller only supplies what only IT knows: what this task is doing. `Stage` must be static
	 *  storage. */
	void Submit(const char* Stage, std::function<void()> Task);

	/** Barrier: block until every task submitted before this call completed. */
	void Flush();

	/** True when the CALLER is this server's own worker thread. The task body runs there, so a role
	 *  that must reach its own thread (a marshal helper) uses this to run inline instead of posting a
	 *  task and waiting on itself. */
	[[nodiscard]] bool IsServerThread() const;

protected:
	/** Called before the thread starts; return false to abort. */
	[[nodiscard]] virtual bool OnInitialize();

	/** Called after the thread joins. */
	virtual void OnShutdown();

	[[nodiscard]] virtual const char* GetThreadName() const;

private:
	/** One queue entry: the work plus the label to draw it under. The role half of the label is
	 *  the server's own (GetThreadName), so only the stage is stored per task. */
	struct FQueuedTask
	{
		const char*           Stage = "Task";
		std::function<void()> Task;
	};

	void RunLoop();

	std::thread Worker;
	std::thread::id WorkerId;   // set by RunLoop (its own id); guarded by Mutex
	std::deque<FQueuedTask> Queue;
	mutable std::mutex Mutex;   // mutable: const IsServerThread() reads WorkerId under it
	std::condition_variable CondVar;
	std::atomic<bool> bRunning{false};
	bool bStopping = false;
};

} // namespace Maho
