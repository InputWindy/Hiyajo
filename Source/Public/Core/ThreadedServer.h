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
#include <Core/Fatal.h>

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
class FThreadedServer
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
	[[nodiscard]] virtual bool OnInitialize()
	{
		return true;
	}

	/** Called after the thread joins. */
	virtual void OnShutdown()
	{
	}

	[[nodiscard]] virtual const char* GetThreadName() const
	{
		return "ThreadedServer";
	}

private:
	void RunLoop();

	std::thread Worker;
	std::deque<std::function<void()>> Queue;
	std::mutex Mutex;
	std::condition_variable CondVar;
	std::atomic<bool> bRunning{false};
	bool bStopping = false;
};

inline FThreadedServer::~FThreadedServer()
{
	Shutdown();
}

inline bool FThreadedServer::Initialize()
{
	if (bRunning.load(std::memory_order_acquire))
	{
		return true;
	}
	if (!OnInitialize())
	{
		return false;
	}
	bStopping = false;
	bRunning.store(true, std::memory_order_release);
	Worker = std::thread(&FThreadedServer::RunLoop, this);
	return true;
}

inline void FThreadedServer::Shutdown()
{
	if (!bRunning.load(std::memory_order_acquire))
	{
		return;
	}
	{
		std::lock_guard Lock(Mutex);
		bStopping = true;
	}
	CondVar.notify_all();
	if (Worker.joinable())
	{
		Worker.join();
	}
	bRunning.store(false, std::memory_order_release);
	OnShutdown();
}

inline void FThreadedServer::Submit(std::function<void()> Task)
{
	{
		std::lock_guard Lock(Mutex);
		Queue.push_back(std::move(Task));
	}
	CondVar.notify_one();
}

inline void FThreadedServer::Flush()
{
	std::mutex BarrierMutex;
	std::condition_variable BarrierCv;
	bool bDone = false;
	Submit([&]
	{
		std::lock_guard Lock(BarrierMutex);
		bDone = true;
		BarrierCv.notify_all();
	});
	std::unique_lock Lock(BarrierMutex);
	BarrierCv.wait(Lock, [&] { return bDone; });
}

inline void FThreadedServer::RunLoop()
{
	while (true)
	{
		std::function<void()> Task;
		{
			std::unique_lock Lock(Mutex);
			CondVar.wait(Lock, [this] { return bStopping || !Queue.empty(); });
			if (bStopping && Queue.empty())
			{
				return;
			}
			Task = std::move(Queue.front());
			Queue.pop_front();
		}
		try
		{
			Task();
		}
		catch (const std::exception& E)
		{
			// An exception escaping the dedicated worker bypasses the main-thread
			// try-catch; report it here.
			ReportFatal(E.what());
		}
		catch (...)
		{
			ReportFatal("Unknown exception in threaded server");
		}
	}
}

} // namespace Maho
