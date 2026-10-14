#include <Core/ThreadedServer.h>

#include <Core/Fatal.h>
#include <Core/ThreadPool.h>

#include <atomic>
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
	// OwnedPool is the fallback -- a server nobody handed a pool to (a test, a standalone tool) still
	// runs its lane here. Only ONE lane of it is ever used, and a persistent lane needs no workers, so
	// this costs a thread and nothing else.
	FThreadPool       OwnedPool;
	FThreadPool*      Pool = &OwnedPool;
	FThreadPool::FLane Lane = FThreadPool::InvalidLane;
	std::atomic<bool> bRunning{ false };
};

FThreadedServer::FThreadedServer()
	: Impl(std::make_unique<FImpl>())
{
}

FThreadedServer::~FThreadedServer()
{
	Shutdown();
}

void FThreadedServer::BindPool(FThreadPool& InPool)
{
	if (Impl->bRunning.load(std::memory_order_acquire))
	{
		ReportError("FThreadedServer::BindPool after Initialize: the lane is already running");
		return;
	}
	Impl->Pool = &InPool;
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

	// A PERSISTENT lane: this role's work must run on one fixed thread (see the header).
	Impl->Lane = Impl->Pool->CreatePersistentLane(GetThreadName());
	if (Impl->Lane == FThreadPool::InvalidLane)
	{
		OnShutdown();
		return false;
	}
	Impl->bRunning.store(true, std::memory_order_release);
	return true;
}

void FThreadedServer::Shutdown()
{
	if (!Impl->bRunning.load(std::memory_order_acquire))
	{
		return;
	}

	Impl->Pool->Flush(Impl->Lane);                    // the lane drains before its thread leaves
	Impl->Pool->DestroyPersistentLane(Impl->Lane);
	Impl->Lane = FThreadPool::InvalidLane;
	Impl->bRunning.store(false, std::memory_order_release);
	OnShutdown();
}

bool FThreadedServer::IsRunning() const
{
	return Impl->bRunning.load(std::memory_order_acquire);
}

void FThreadedServer::Submit(std::function<void()> Task)
{
	if (!Impl->bRunning.load(std::memory_order_acquire))
	{
		// Loud, not silent: a task queued at a role that is not running would simply never run, and
		// "nothing happens" is the hardest kind of bug to find (it used to be accepted and dropped).
		ReportError("FThreadedServer::Submit: the role is not running (before Initialize or after "
			"Shutdown); the task was dropped");
		return;
	}
	Impl->Pool->Submit(Impl->Lane, std::move(Task));
}

void FThreadedServer::Flush()
{
	if (!Impl->bRunning.load(std::memory_order_acquire))
	{
		return;
	}
	Impl->Pool->Flush(Impl->Lane);
}

bool FThreadedServer::IsServerThread() const
{
	if (!Impl->bRunning.load(std::memory_order_acquire))
	{
		return false;
	}
	return Impl->Pool->IsCurrentThread(Impl->Lane);
}

}
