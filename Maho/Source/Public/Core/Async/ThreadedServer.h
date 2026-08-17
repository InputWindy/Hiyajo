#pragma once

#include <Core/Async/Runable.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace Maho
{

/**
 * Dedicated resident worker: one persistent thread + FIFO task queue.
 *
 * For long-lived ROLES (render thread, IO thread, ...) that need a private,
 * always-on thread with a serial command queue — NOT for transient parallel
 * work (use FThreadPool for that).
 *
 * Subclass + override OnInitialize / OnShutdown / GetThreadName for role-
 * specific setup (e.g. FRenderSystem creates its RHI in OnInitialize).
 *
 * Flush() is a FIFO barrier: every task submitted before Flush completes
 * before Flush returns (the queue is strictly serial).
 */
class FThreadedServer : public IRunable
{
public:
	FThreadedServer() = default;
	virtual ~FThreadedServer();

	FThreadedServer(const FThreadedServer&) = delete;
	FThreadedServer& operator=(const FThreadedServer&) = delete;

	bool Initialize();
	void Shutdown();

	/** The resident worker loop; runs on the dedicated thread. */
	void MainLoop() override;

	/** Background workers don't parse the command line. */
	void ParseCommandLine(int Argc, char** Argv) override
	{
		(void)Argc;
		(void)Argv;
	}

	/** Request the worker loop to exit (non-blocking; Shutdown() joins). */
	void RequestShutdown() override
	{
		{
			std::lock_guard Lock(Mutex);
			bStopping = true;
		}
		CondVar.notify_all();
	}

	[[nodiscard]] bool IsRunning() const
	{
		return bRunning.load(std::memory_order_acquire);
	}

	/** Enqueue one task (non-blocking, FIFO). */
	void Submit(std::function<void()> Task);

	/** Barrier: block until every task submitted before this call has completed. */
	void Flush();

protected:
	[[nodiscard]] virtual bool OnInitialize()
	{
		return true;
	}

	virtual void OnShutdown()
	{
	}

	[[nodiscard]] virtual const char* GetThreadName() const
	{
		return "ThreadedServer";
	}

private:
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
	Worker = std::thread(&FThreadedServer::MainLoop, this);
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

inline void FThreadedServer::MainLoop()
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
		Task();
	}
}

} // namespace Maho
