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

	/** Enqueue one task (non-blocking, FIFO, serial execution). */
	void Submit(std::function<void()> Task);

	/** Barrier: block until every task submitted before this call completed. */
	void Flush();

protected:
	/** Called before the thread starts; return false to abort. */
	[[nodiscard]] virtual bool OnInitialize();

	/** Called after the thread joins. */
	virtual void OnShutdown();

	[[nodiscard]] virtual const char* GetThreadName() const;

private:
	void RunLoop();

	std::thread Worker;
	std::deque<std::function<void()>> Queue;
	std::mutex Mutex;
	std::condition_variable CondVar;
	std::atomic<bool> bRunning{false};
	bool bStopping = false;
};

} // namespace Maho
