#include <Core/Task/ThreadPool.h>

#include <Core/Fatal.h>

#include <array>
#include <atomic>
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

		/** This lane's OWN wake-up, for a persistent lane.
		 *
		 *  It must not share the workers' `CondVar`: a wake-up posted for a SHARED lane is a single
		 *  `notify_one`, and a pinned thread waiting there would consume it, find nothing it may take, and go
		 *  back to sleep -- leaving the shared lane's work queued with every worker asleep. That was a real
		 *  intermittent stall (a fresh graph's first node never starting until something else notified). */
		std::condition_variable Wake;
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

	/// @brief How many shared workers are inside a task body right now (diagnostics: "are they all busy?").
	std::atomic<std::uint32_t> BusyWorkers{ 0 };

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
				Lane.Wake.notify_all();     // 每条 pinned 线程只等自己的 Wake（见 NotifyWorkAvailableLocked）
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

std::uint32_t FThreadPool::GetBusyWorkers() const
{
	return Impl->BusyWorkers.load();
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
		State.Wake.notify_all();                   // its OWN thread waits there (see FLaneState::Wake)
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
			Lanes[Lane].Wake.wait(Lock, [this, Lane]
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

		BusyWorkers.fetch_add(1);
		RunTaskSafely(Owner, Queued.Task);
		BusyWorkers.fetch_sub(1);

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
	// A persistent lane has exactly ONE possible server: its own thread. Wake it on its OWN condition variable,
	// never on the pool's -- the reverse mistake is just as bad (a pinned thread consuming the wake-up meant for
	// a shared lane leaves the work queued with every worker asleep), and separating them removes both.
	if (Lanes[Lane].bPersistent)
	{
		Lanes[Lane].Wake.notify_one();
		return;
	}

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

// ── FThreadedServer: a resident role on the pool above ──────────────────────────

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
