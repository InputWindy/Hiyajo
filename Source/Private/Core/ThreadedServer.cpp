#include <Core/ThreadedServer.h>

#include <Core/Fatal.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Maho
{

bool FThreadedServer::OnInitialize()
{
	return true;
}

void FThreadedServer::OnShutdown()
{
}

const char* FThreadedServer::GetThreadName() const
{
	return "ThreadedServer";
}

struct FThreadedServer::FImpl
{
	struct FQueuedTask
	{
		std::function<void()> Task;
	};

	void RunLoop();

	std::thread Worker;
	std::thread::id WorkerId;
	std::deque<FQueuedTask> Queue;
	mutable std::mutex Mutex;
	std::condition_variable CondVar;
	std::atomic<bool> bRunning{false};
	bool bStopping = false;
};

FThreadedServer::FThreadedServer()
	: Impl(std::make_unique<FImpl>())
{
}

FThreadedServer::~FThreadedServer()
{
	Shutdown();
}

bool FThreadedServer::Initialize()
{
	if (Impl->bRunning.load(std::memory_order_acquire))
	{
		return true;
	}
	if (!OnInitialize())
	{
		return false;
	}
	Impl->bStopping = false;
	Impl->bRunning.store(true, std::memory_order_release);
	Impl->Worker = std::thread(&FImpl::RunLoop, Impl.get());
	return true;
}

void FThreadedServer::Shutdown()
{
	if (!Impl->bRunning.load(std::memory_order_acquire))
	{
		return;
	}
	{
		std::lock_guard Lock(Impl->Mutex);
		Impl->bStopping = true;
	}
	Impl->CondVar.notify_all();
	if (Impl->Worker.joinable())
	{
		Impl->Worker.join();
	}
	Impl->bRunning.store(false, std::memory_order_release);
	OnShutdown();
}

bool FThreadedServer::IsRunning() const
{
	return Impl->bRunning.load(std::memory_order_acquire);
}

void FThreadedServer::Submit(std::function<void()> Task)
{
	{
		std::lock_guard Lock(Impl->Mutex);
		Impl->Queue.push_back(FImpl::FQueuedTask{ std::move(Task) });
	}
	Impl->CondVar.notify_one();
}

void FThreadedServer::Flush()
{
	if (!IsRunning())
	{
		return;
	}

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

void FThreadedServer::FImpl::RunLoop()
{
	{
		std::lock_guard Lock(Mutex);
		WorkerId = std::this_thread::get_id();
	}

	while (true)
	{
		FQueuedTask Queued;
		{
			std::unique_lock Lock(Mutex);

			CondVar.wait(Lock, [this] { return bStopping || !Queue.empty(); });

			if (bStopping && Queue.empty())
			{
				break;
			}
			Queued = std::move(Queue.front());
			Queue.pop_front();
		}

		try
		{
			Queued.Task();
		}
		catch (const std::exception& E)
		{
			ReportError(E.what());
		}
		catch (...)
		{
			ReportError("Unknown exception in threaded server");
		}
	}

	{
		std::lock_guard Lock(Mutex);
		WorkerId = std::thread::id{};
	}
}

bool FThreadedServer::IsServerThread() const
{
	std::lock_guard Lock(Impl->Mutex);
	return Impl->WorkerId != std::thread::id{} && Impl->WorkerId == std::this_thread::get_id();
}

}
