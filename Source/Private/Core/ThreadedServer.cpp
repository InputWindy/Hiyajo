#include <Core/ThreadedServer.h>

#include <Core/Fatal.h>

#include <stdexcept>
#include <utility>

namespace Maho
{

// The virtuals are defined HERE rather than inline: this class is a base of
// plugin-side objects (a render/IO role) and of the scheduler, so keeping its
// first out-of-line virtual in the engine's own module gives it a KEY FUNCTION --
// one vtable and one deleting destructor, in Maho.dll, instead of a COMDAT copy
// per module. The inline versions were exactly the hazard Core/Export.h warns
// about (whichever TU instantiated it stamped its own module's vptr).
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

FThreadedServer::~FThreadedServer()
{
	Shutdown();
}

bool FThreadedServer::Initialize()
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

void FThreadedServer::Shutdown()
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

void FThreadedServer::Submit(std::function<void()> Task)
{
	{
		std::lock_guard Lock(Mutex);
		Queue.push_back(FQueuedTask{ std::move(Task) });
	}
	CondVar.notify_one();
}

void FThreadedServer::Flush()
{
	// A barrier means "wait until the work I queued has run" -- and with no worker there is nothing
	// that will ever run it, so waiting would hang forever. (Found the hard way: a caller that
	// drained a server AFTER Shutdown -- the thread is joined, the queue is dead -- blocked for good.)
	// The server never refuses work, but it also must not pretend a barrier can be honoured here.
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

void FThreadedServer::RunLoop()
{
	// Publish this thread's identity for IsServerThread(): a marshal helper on the server thread
	// must recognize itself and run inline instead of posting a task and waiting on it forever.
	{
		std::lock_guard Lock(Mutex);
		WorkerId = std::this_thread::get_id();
	}

	while (true)
	{
		FQueuedTask Queued;
		{
			std::unique_lock Lock(Mutex);

			// No bar for the WAIT itself -- the server runs closures and knows nothing about any
			// profiling layer (a role that wants its work visible instruments the body it submits).
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
			// A throwing task must not kill the host -- report (non-fatal) and
			// keep serving.
			ReportError(E.what());
		}
		catch (...)
		{
			ReportError("Unknown exception in threaded server");
		}
	}

	// Retract this thread's identity: the id may be recycled by the OS for a later thread, and a
	// stale match would make a marshal helper believe it is already on the server.
	{
		std::lock_guard Lock(Mutex);
		WorkerId = std::thread::id{};
	}
}

bool FThreadedServer::IsServerThread() const
{
	std::lock_guard Lock(Mutex);
	return WorkerId != std::thread::id{} && WorkerId == std::this_thread::get_id();
}

} // namespace Maho
