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

		// Persistent lanes: their own thread serves them and only them (see CreatePersistentLane).
		bool                    bPersistent = false;
		bool                    bStopping   = false;
		std::thread             Pinned;
		std::thread::id         PinnedId{};
		const char*             Name = "";
	};

	void EnsureThreads(const FThreadPool* Owner, std::uint32_t Required);

	void WorkerLoop(const FThreadPool* Owner);

	/** The thread of a PERSISTENT lane: serves that one lane until it is told to stop, draining what
	 *  is already queued before it returns (a lane is never dropped with work still in it). */
	void PinnedLoop(const FThreadPool* Owner, FLane Lane);

	bool TakeAnyTaskLocked(FQueuedTask& OutTask, FLane& OutLane);

	[[nodiscard]] bool AnyLaneHasWorkLocked() const;

	/** Wake whoever can serve the lane that just received work. A SINGLE wake-up is enough for shared
 *  lanes, but never for a PERSISTENT one: that wake-up may land on a shared worker, which finds
 *  nothing it may take and goes back to sleep -- leaving the pinned thread asleep with work queued
 *  and `Flush(lane)` waiting forever. */
void NotifyWorkAvailableLocked(FLane Lane);

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
		for (FImpl::FLaneState& Lane : Impl->Lanes)
		{
			if (Lane.bPersistent)
			{
				Lane.bStopping = true;      // 持久线程会先把它队列里的活干完再退出
			}
		}
	}
	Impl->CondVar.notify_all();
	for (std::thread& Worker : Impl->Workers)
	{
		if (Worker.joinable())
		{
			Worker.join();
		}
	}
	for (FImpl::FLaneState& Lane : Impl->Lanes)
	{
		if (Lane.Pinned.joinable())
		{
			Lane.Pinned.join();
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


FThreadPool::FLane FThreadPool::CreatePersistentLane(const char* ThreadName)
{
	std::lock_guard Lock(Impl->Mutex);

	for (FLane Lane = 1; Lane < MaxLanes; ++Lane)
	{
		FImpl::FLaneState& State = Impl->Lanes[Lane];
		if (State.bInUse)
		{
			continue;
		}
		State.Queue.clear();
		State.Pending    = 0;
		State.bInUse     = true;
		State.bPersistent = true;
		State.bStopping  = false;
		State.PinnedId   = std::thread::id{};
		State.Name       = ThreadName != nullptr ? ThreadName : "PersistentLane";
		// 线程启动后第一件事是抢 Mutex 记下自己的 id —— 我们立刻放开，因此不会死锁（也不 join）。
		State.Pinned     = std::thread(&FImpl::PinnedLoop, Impl.get(), this, Lane);
		return Lane;
	}

	ReportError("FThreadPool::CreatePersistentLane: lanes are at capacity; no dedicated thread");
	return InvalidLane;
}

void FThreadPool::DestroyPersistentLane(FLane Lane)
{
	if (Lane == DefaultLane || Lane >= MaxLanes)
	{
		return;
	}

	std::thread Pinned;
	{
		std::lock_guard Lock(Impl->Mutex);
		FImpl::FLaneState& State = Impl->Lanes[Lane];
		if (!State.bInUse || !State.bPersistent)
		{
			return;
		}
		State.bStopping = true;
		Impl->CondVar.notify_all();
		Pinned = std::move(State.Pinned);          // join 必须在锁外
	}
	if (Pinned.joinable())
	{
		Pinned.join();
	}

	std::lock_guard Lock(Impl->Mutex);
	FImpl::FLaneState& State = Impl->Lanes[Lane];
	State.bPersistent = false;
	State.bStopping   = false;
	State.PinnedId    = std::thread::id{};
	State.Name        = "";
	State.bInUse      = false;
}

bool FThreadPool::IsCurrentThread(FLane Lane) const
{
	std::lock_guard Lock(Impl->Mutex);
	if (Lane >= MaxLanes)
	{
		return false;
	}
	const FImpl::FLaneState& State = Impl->Lanes[Lane];
	return State.bInUse && State.bPersistent && State.PinnedId != std::thread::id{}
		&& State.PinnedId == std::this_thread::get_id();
}

void FThreadPool::FImpl::PinnedLoop(const FThreadPool* Owner, FLane Lane)
{
	{
		std::lock_guard Lock(Mutex);
		Lanes[Lane].PinnedId = std::this_thread::get_id();
	}

	while (true)
	{
		FQueuedTask Queued;
		{
			std::unique_lock Lock(Mutex);
			CondVar.wait(Lock, [this, Lane]
				{
					return Lanes[Lane].bStopping || !Lanes[Lane].Queue.empty();
				});
			if (Lanes[Lane].Queue.empty())
			{
				if (Lanes[Lane].bStopping)
				{
					Lanes[Lane].PinnedId = std::thread::id{};
					return;
				}
				continue;
			}
			Queued = std::move(Lanes[Lane].Queue.front());
			Lanes[Lane].Queue.pop_front();
		}

		RunTaskSafely(Owner, Queued.Task);

		{
			std::lock_guard Lock(Mutex);
			Lanes[Lane].Pending -= 1;
			NotifyProgressLocked();
		}
	}
}


void FThreadPool::Submit(FLane Lane, std::function<void()> Task)
{
	// 持久 lane 由它自己的线程消费 —— 不必为它拉起 worker（那只会造出一堆空转线程）
	bool bPinnedLane = false;
	{
		std::lock_guard Lock(Impl->Mutex);
		bPinnedLane = Lane < MaxLanes && Impl->Lanes[Lane].bInUse && Impl->Lanes[Lane].bPersistent;
	}
	if (!bPinnedLane)
	{
		Impl->EnsureThreads(this, Impl->NumThreads);
	}

	std::lock_guard Lock(Impl->Mutex);
	if (Lane >= MaxLanes || !Impl->Lanes[Lane].bInUse)
	{
		ReportError("FThreadPool::Submit: unknown lane, falling back to the default lane");
		Lane = DefaultLane;
	}
	Impl->Lanes[Lane].Queue.push_back(FImpl::FQueuedTask{ std::move(Task) });
	Impl->Lanes[Lane].Pending += 1;
	Impl->NotifyWorkAvailableLocked(Lane);
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

	// 只有"调用者本来就在本池里跑"时才可以顺手排空 —— 但持久 lane 不行：它的活必须由它自己的
	// 线程执行，别的线程替他跑就违反了亲和（这正是持久 lane 存在的理由）。
	const bool bMayHelpDrain = (GRunningPool == this) && !Impl->Lanes[Lane].bPersistent;

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
		// 持久 lane 不算：它的活只由它自己的线程取（见 CreatePersistentLane）
		if (Lane.bPersistent)
		{
			continue;
		}
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
		if (Lanes[NextLaneToServe].bPersistent)
		{
			continue;                    // 亲和：共享 worker 绝不碰持久 lane
		}
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

void FThreadPool::FImpl::NotifyWorkAvailableLocked(FLane Lane)
{
	if (Lanes[Lane].bPersistent || FlushWaiters > 0)
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
