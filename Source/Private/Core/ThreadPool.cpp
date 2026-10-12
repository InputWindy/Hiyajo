#include <Core/ThreadPool.h>

#include <Core/Fatal.h>

#include <array>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace Maho
{

namespace
{
	thread_local const FThreadPool* GRunningPool = nullptr;
}

struct FThreadPool::FImpl
{
	struct FQueuedTask
	{
		std::function<void()> Task;
	};

	struct FLaneState
	{
		std::deque<FQueuedTask> Queue;
		std::uint32_t           Pending = 0;
		bool                    bInUse  = false;
	};

	void EnsureThreads(const FThreadPool* Owner, std::uint32_t Required);

	void WorkerLoop(const FThreadPool* Owner);

	bool TakeAnyTaskLocked(FQueuedTask& OutTask, FLane& OutLane);

	[[nodiscard]] bool AnyLaneHasWorkLocked() const;

	void NotifyWorkAvailableLocked();

	void NotifyProgressLocked();

	std::vector<std::thread> Workers;

	mutable std::mutex      Mutex;
	std::condition_variable CondVar;

	std::uint32_t NumThreads = 0;
	bool          bStopping  = false;

	std::array<FLaneState, MaxLanes> Lanes{};

	FLane NextLaneToServe = DefaultLane;

	std::uint32_t FlushWaiters = 0;
};

namespace
{
	void RunTaskSafely(const FThreadPool* Owner, const std::function<void()>& Task)
	{
		const FThreadPool* const Previous = GRunningPool;
		GRunningPool = Owner;
		try
		{
			Task();
		}
		catch (const std::exception& E)
		{
			ReportError(E.what());
		}
		catch (...)
		{
			ReportError("Unknown exception in thread-pool worker");
		}
		GRunningPool = Previous;
	}
}

FThreadPool::FThreadPool(std::uint32_t InNumThreads)
	: Impl(std::make_unique<FImpl>())
{
	Impl->NumThreads = InNumThreads == 0 ? std::thread::hardware_concurrency() : InNumThreads;
	if (Impl->NumThreads == 0)
	{
		Impl->NumThreads = 1;
	}
	if (InNumThreads == 0)
	{
		if (const char* Override = std::getenv("MAHO_PARALLELISM"))
		{
			const long Requested = std::strtol(Override, nullptr, 10);
			if (Requested > 0)
			{
				Impl->NumThreads = static_cast<std::uint32_t>(Requested);
			}
		}
	}
	Impl->Workers.reserve(Impl->NumThreads);
	Impl->Lanes[DefaultLane].bInUse = true;
}

FThreadPool::~FThreadPool()
{
	{
		std::lock_guard Lock(Impl->Mutex);
		Impl->bStopping = true;
	}
	Impl->CondVar.notify_all();
	for (std::thread& Worker : Impl->Workers)
	{
		if (Worker.joinable())
		{
			Worker.join();
		}
	}
}

FThreadPool::FLane FThreadPool::CreateLane()
{
	std::lock_guard Lock(Impl->Mutex);

	for (FLane Lane = 1; Lane < MaxLanes; ++Lane)
	{
		if (!Impl->Lanes[Lane].bInUse)
		{
			Impl->Lanes[Lane].Queue.clear();
			Impl->Lanes[Lane].Pending = 0;
			Impl->Lanes[Lane].bInUse = true;
			return Lane;
		}
	}

	ReportError("FThreadPool::CreateLane: at capacity, the caller shares the default lane");
	return DefaultLane;
}

void FThreadPool::DestroyLane(FLane Lane)
{
	if (Lane == DefaultLane || Lane >= MaxLanes)
	{
		return;
	}

	std::lock_guard Lock(Impl->Mutex);
	if (!Impl->Lanes[Lane].bInUse)
	{
		return;
	}
	if (Impl->Lanes[Lane].Pending != 0 || !Impl->Lanes[Lane].Queue.empty())
	{
		ReportError("FThreadPool::DestroyLane: lane still has work, it will not be reclaimed");
		return;
	}
	Impl->Lanes[Lane].bInUse = false;
}

std::uint32_t FThreadPool::GetLanePending(FLane Lane) const
{
	std::lock_guard Lock(Impl->Mutex);
	if (Lane >= MaxLanes || !Impl->Lanes[Lane].bInUse)
	{
		return 0;
	}
	return Impl->Lanes[Lane].Pending;
}

std::uint32_t FThreadPool::GetNumThreads() const
{
	return Impl->NumThreads;
}


void FThreadPool::Submit(std::function<void()> Task)
{
	Submit(DefaultLane, std::move(Task));
}

void FThreadPool::Submit(FLane Lane, std::function<void()> Task)
{
	Impl->EnsureThreads(this, Impl->NumThreads);

	std::lock_guard Lock(Impl->Mutex);
	if (Lane >= MaxLanes || !Impl->Lanes[Lane].bInUse)
	{
		ReportError("FThreadPool::Submit: unknown lane, falling back to the default lane");
		Lane = DefaultLane;
	}
	Impl->Lanes[Lane].Queue.push_back(FImpl::FQueuedTask{ std::move(Task) });
	Impl->Lanes[Lane].Pending += 1;
	Impl->NotifyWorkAvailableLocked();
}


void FThreadPool::Flush()
{
	Flush(DefaultLane);
}

void FThreadPool::Flush(FLane Lane)
{
	if (Lane >= MaxLanes)
	{
		return;
	}

	std::unique_lock Lock(Impl->Mutex);
	if (!Impl->Lanes[Lane].bInUse)
	{
		return;
	}

	const bool bMayHelpDrain = (GRunningPool == this);

	Impl->FlushWaiters += 1;
	for (;;)
	{
		if (Impl->Lanes[Lane].Pending == 0)
		{
			break;
		}

		if (bMayHelpDrain && !Impl->Lanes[Lane].Queue.empty())
		{
			FImpl::FQueuedTask Queued = std::move(Impl->Lanes[Lane].Queue.front());
			Impl->Lanes[Lane].Queue.pop_front();
			Lock.unlock();
			RunTaskSafely(this, Queued.Task);
			Lock.lock();
			Impl->Lanes[Lane].Pending -= 1;
			Impl->NotifyProgressLocked();
			continue;
		}

		Impl->CondVar.wait(Lock);
	}
	Impl->FlushWaiters -= 1;
}


void FThreadPool::FImpl::EnsureThreads(const FThreadPool* Owner, std::uint32_t Required)
{
	if (Required > NumThreads)
	{
		Required = NumThreads;
	}
	std::lock_guard Lock(Mutex);
	while (Workers.size() < Required)
	{
		Workers.emplace_back(&FImpl::WorkerLoop, this, Owner);
	}
}

void FThreadPool::FImpl::WorkerLoop(const FThreadPool* Owner)
{
	while (true)
	{
		FQueuedTask Queued;
		FLane TaskLane = DefaultLane;
		{
			std::unique_lock Lock(Mutex);
			CondVar.wait(Lock, [this] { return bStopping || AnyLaneHasWorkLocked(); });
			if (bStopping && !AnyLaneHasWorkLocked())
			{
				return;
			}
			if (!TakeAnyTaskLocked(Queued, TaskLane))
			{
				continue;
			}
		}

		RunTaskSafely(Owner, Queued.Task);

		{
			std::lock_guard Lock(Mutex);
			Lanes[TaskLane].Pending -= 1;
			NotifyProgressLocked();
		}
	}
}

bool FThreadPool::FImpl::AnyLaneHasWorkLocked() const
{
	for (const FLaneState& Lane : Lanes)
	{
		if (!Lane.Queue.empty())
		{
			return true;
		}
	}
	return false;
}

bool FThreadPool::FImpl::TakeAnyTaskLocked(FQueuedTask& OutTask, FLane& OutLane)
{
	for (std::uint32_t Step = 0; Step < MaxLanes; ++Step)
	{
		NextLaneToServe = static_cast<FLane>((NextLaneToServe + 1) % MaxLanes);
		if (!Lanes[NextLaneToServe].Queue.empty())
		{
			OutLane = NextLaneToServe;
			OutTask = std::move(Lanes[NextLaneToServe].Queue.front());
			Lanes[NextLaneToServe].Queue.pop_front();
			return true;
		}
	}
	return false;
}

void FThreadPool::FImpl::NotifyWorkAvailableLocked()
{
	if (FlushWaiters > 0)
	{
		CondVar.notify_all();
		return;
	}
	CondVar.notify_one();
}

void FThreadPool::FImpl::NotifyProgressLocked()
{
	if (FlushWaiters > 0)
	{
		CondVar.notify_all();
	}
}

}
