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
		Queue.push_back(std::move(Task));
	}
	CondVar.notify_one();
}

void FThreadedServer::Flush()
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

void FThreadedServer::RunLoop()
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
			// A throwing task must not kill the host -- report (non-fatal) and
			// keep serving.
			ReportError(E.what());
		}
		catch (...)
		{
			ReportError("Unknown exception in threaded server");
		}
	}
}

} // namespace Maho
