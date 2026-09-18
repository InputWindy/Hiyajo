#include <Core/ThreadedServer.h>

#include <Core/Fatal.h>
#include <Core/Profiler.h>

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
	Submit("Task", std::move(Task));
}

void FThreadedServer::Submit(const char* Stage, std::function<void()> Task)
{
	{
		std::lock_guard Lock(Mutex);
		Queue.push_back(FQueuedTask{ Stage != nullptr ? Stage : "Task", std::move(Task) });
	}
	CondVar.notify_one();
}

void FThreadedServer::Flush()
{
	// Traced from the CALLER's side: this is where the barrier's cost is paid, and it lands on
	// whatever lane the caller is already on -- the server's own row shows the task bar this
	// wait is waiting behind, so the two line up visually. A plain single-name scope (not a lane
	// scope): switching lanes here would move the stall onto the server's row, which is not where
	// the time is spent.
	MAHO_TRACE_SCOPE("ThreadedServer::Flush");

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
	Submit("Flush", [&]
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
	// The row this thread's events land on, named after the ROLE (GetThreadName). It is the
	// thread's own lane: a server is a sequential stream of tasks, which is exactly what a lane
	// is. Events keep the pointer, so the name must be static storage -- which GetThreadName's
	// contract requires anyway.
	const char* const ThreadName = GetThreadName();

	// Publish this thread's identity for IsServerThread(): a marshal helper on the server thread
	// must recognize itself and run inline instead of posting a task and waiting on it forever.
	{
		std::lock_guard Lock(Mutex);
		WorkerId = std::this_thread::get_id();
	}

	// A one-shot marker, before the first wait, so this thread's row exists even if it is never
	// given a task. Rows are derived from events, so a server nobody submits to would otherwise
	// be INVISIBLE -- and "this resident thread exists and does nothing" is exactly the kind of
	// finding a profile is asked for. One event per thread for the whole run.
	{
		MAHO_TRACE_SCOPE_LANE(ThreadName, "Started");
	}

	while (true)
	{
		FQueuedTask Queued;
		{
			std::unique_lock Lock(Mutex);

			// No bar for the WAIT itself. A gap in this row already means "waiting", which is
			// what a timeline is for, and the metadata declares the row either way -- so an
			// idle bar would double the event count on the busiest lane to say nothing new.
			// (The scheduler thread wakes ~30 times a frame; at 200 fps that is 6000 bars/s.)
			CondVar.wait(Lock, [this] { return bStopping || !Queue.empty(); });

			if (bStopping && Queue.empty())
			{
				break;
			}
			Queued = std::move(Queue.front());
			Queue.pop_front();
		}

		// One bar per task, opened HERE rather than at the submitter's call site: this is the
		// point every server task passes through, and the label the submitter handed over is
		// exactly what the bar needs. The scope also tells the tracer which row this is.
		MAHO_TRACE_SCOPE_LANE(ThreadName, Queued.Stage);
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
