#include <Core/ThreadPool.h>

#include <Core/Fatal.h>

#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace Maho
{

namespace
{
	/** The pool whose task THIS thread is currently executing, or null outside any pool task.
	 *
	 *  It is what licenses Flush to help drain (only a worker of the pool may run its lanes'
	 *  tasks) and it is deliberately a property of the RUNNING TASK, not of the thread: a thread
	 *  that is not running this pool's work -- the game thread, a shutdown path -- must stay out
	 *  of the queue, because running node bodies there is exactly what the pool exists to avoid.
	 *
	 *  Saved and restored around every task, because a drained task can belong to another lane or
	 *  (in a nested case) another pool. */
	thread_local const FThreadPool* GRunningPool = nullptr;
}

FThreadPool::FThreadPool(std::uint32_t InNumThreads)
	: NumThreads(InNumThreads == 0 ? std::thread::hardware_concurrency() : InNumThreads)
{
	if (NumThreads == 0)
	{
		NumThreads = 1;
	}
	// Zero workers at construction -- lazily started on first Submit.
	// Diagnostic override (default-constructed pools only, so an explicitly sized
	// pool such as RHI's serial recording pool keeps its width): MAHO_PARALLELISM=1
	// reproduces the serial execution the graph was historically run under, which
	// makes a parallel failure directly comparable against a known-good baseline.
	if (InNumThreads == 0)
	{
		if (const char* Override = std::getenv("MAHO_PARALLELISM"))
		{
			const long Requested = std::strtol(Override, nullptr, 10);
			if (Requested > 0)
			{
				NumThreads = static_cast<std::uint32_t>(Requested);
			}
		}
	}
	Workers.reserve(NumThreads);
	Lanes[DefaultLane].bInUse = true;   // lane 0 belongs to the pool itself
}

FThreadPool::~FThreadPool()
{
	{
		std::lock_guard Lock(Mutex);
		bStopping = true;
	}
	CondVar.notify_all();
	for (std::thread& Worker : Workers)
	{
		if (Worker.joinable())
		{
			Worker.join();
		}
	}
}

void FThreadPool::EnsureThreads(std::uint32_t Required)
{
	// Never exceed the hardware_concurrency-derived cap (more threads than
	// cores only adds contention).
	if (Required > NumThreads)
	{
		Required = NumThreads;
	}
	std::lock_guard Lock(Mutex);
	while (Workers.size() < Required)
	{
		Workers.emplace_back(&FThreadPool::WorkerLoop, this);
	}
}

// ── lanes ────────────────────────────────────────────────────────────────────

FThreadPool::FLane FThreadPool::CreateLane()
{
	std::lock_guard Lock(Mutex);

	// Lane 0 is the pool's own; hand out from 1 upward.
	for (FLane Lane = 1; Lane < MaxLanes; ++Lane)
	{
		if (!Lanes[Lane].bInUse)
		{
			Lanes[Lane].Queue.clear();
			Lanes[Lane].Pending = 0;
			Lanes[Lane].bInUse = true;
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

	std::lock_guard Lock(Mutex);
	if (!Lanes[Lane].bInUse)
	{
		return;
	}
	if (Lanes[Lane].Pending != 0 || !Lanes[Lane].Queue.empty())
	{
		// Refused, not forced: releasing a lane whose work is still counted here would move that
		// work's completion onto a lane somebody else now owns.
		ReportError("FThreadPool::DestroyLane: lane still has work, it will not be reclaimed");
		return;
	}
	Lanes[Lane].bInUse = false;
}

std::uint32_t FThreadPool::GetLanePending(FLane Lane) const
{
	std::lock_guard Lock(Mutex);
	if (Lane >= MaxLanes || !Lanes[Lane].bInUse)
	{
		return 0;
	}
	return Lanes[Lane].Pending;
}

// ── submission ───────────────────────────────────────────────────────────────

void FThreadPool::Submit(std::function<void()> Task)
{
	Submit(DefaultLane, std::move(Task));
}

void FThreadPool::Submit(FLane Lane, std::function<void()> Task)
{
	EnsureThreads(NumThreads);

	std::lock_guard Lock(Mutex);
	if (Lane >= MaxLanes || !Lanes[Lane].bInUse)
	{
		// An unknown lane is a caller bug, but dropping its work would turn it into a hang
		// somewhere else entirely -- report and keep it, on the lane that always exists.
		ReportError("FThreadPool::Submit: unknown lane, falling back to the default lane");
		Lane = DefaultLane;
	}
	Lanes[Lane].Queue.push_back(FQueuedTask{ std::move(Task) });
	Lanes[Lane].Pending += 1;
	NotifyWorkAvailableLocked();
}

// ── barriers ─────────────────────────────────────────────────────────────────

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

	std::unique_lock Lock(Mutex);
	if (!Lanes[Lane].bInUse)
	{
		return;
	}

	// A worker may call this -- a collector's stage body is dispatched by its PARENT's graph, so
	// it runs on the parent's lane while flushing its own, and that lane's count cannot include
	// it. But when the caller IS a worker of this pool, it can and must help drain the lane it is
	// waiting on: a pool whose workers are all blocked here has a queue nobody can run, and then
	// the count below is unreachable no matter what it is.
	const bool bMayHelpDrain = (GRunningPool == this);

	FlushWaiters += 1;
	for (;;)
	{
		if (Lanes[Lane].Pending == 0)
		{
			break;
		}

		if (bMayHelpDrain && !Lanes[Lane].Queue.empty())
		{
			// Drain exactly ONE lane -- the one being waited on. That is what keeps this exact:
			// it advances this condition and touches nothing else's. The task is run exactly like
			// any other one, so a drained task is indistinguishable from a worker-run one -- which
			// is why nothing here (or in the worker) needs to know how a task is instrumented.
			FQueuedTask Queued = std::move(Lanes[Lane].Queue.front());
			Lanes[Lane].Queue.pop_front();
			Lock.unlock();
			RunTaskSafely(Queued.Task);
			Lock.lock();
			Lanes[Lane].Pending -= 1;
			NotifyProgressLocked();
			continue;
		}

		// Re-waiting (not a predicate wait) so that a task arriving while we sleep gets one more
		// look at the drain branch above -- otherwise a full house of blocked workers could sleep
		// through work it is able to run itself.
		CondVar.wait(Lock);
	}
	FlushWaiters -= 1;
}

// ── workers ──────────────────────────────────────────────────────────────────

bool FThreadPool::AnyLaneHasWorkLocked() const
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

bool FThreadPool::TakeAnyTaskLocked(FQueuedTask& OutTask, FLane& OutLane)
{
	// Round-robin from the cursor so a busy lane cannot starve a quiet one (lane 0 carries the
	// host graph and would otherwise always win).
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

void FThreadPool::NotifyWorkAvailableLocked()
{
	if (FlushWaiters > 0)
	{
		// A blocked Flush waiter may need to help drain; it cannot be picked by notify_one.
		CondVar.notify_all();
		return;
	}
	CondVar.notify_one();
}

void FThreadPool::NotifyProgressLocked()
{
	if (FlushWaiters > 0)
	{
		CondVar.notify_all();
	}
}

void FThreadPool::RunTaskSafely(const std::function<void()>& Task)
{
	const FThreadPool* const Previous = GRunningPool;
	GRunningPool = this;
	try
	{
		Task();
	}
	catch (const std::exception& E)
	{
		// A throwing task must not kill the host -- a buggy plugin stage should
		// be isolated. Report (non-fatal) and keep the worker serving.
		ReportError(E.what());
	}
	catch (...)
	{
		ReportError("Unknown exception in thread-pool worker");
	}
	GRunningPool = Previous;
}

void FThreadPool::WorkerLoop()
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

		RunTaskSafely(Queued.Task);

		{
			std::lock_guard Lock(Mutex);
			Lanes[TaskLane].Pending -= 1;
			NotifyProgressLocked();
		}
	}
}

} // namespace Maho
