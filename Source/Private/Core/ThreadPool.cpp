#include <Core/ThreadPool.h>

#include <Core/Fatal.h>

#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace Maho
{

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

void FThreadPool::Submit(std::function<void()> Task)
{
	EnsureThreads(NumThreads);   // lazy-start: bring the whole pool up on first use
	{
		std::lock_guard Lock(Mutex);
		Queue.push_back(std::move(Task));
		PendingCount += 1;
	}
	CondVar.notify_one();
}

void FThreadPool::Flush()
{
	// Wait for true quiescence: the queue empty AND PendingCount zero (a running
	// task only decrements after it COMPLETED, so zero means nothing is in
	// flight). The while-loop re-waits if a concurrent Submit bumps the count
	// while we drain -- a nested graph (e.g. the render graph dispatching its
	// downstream stages) can submit from a worker, and those must not escape.
	std::unique_lock Lock(Mutex);
	while (PendingCount != 0 || !Queue.empty())
	{
		CondVar.wait(Lock, [&] { return PendingCount == 0 && Queue.empty(); });
	}
}

void FThreadPool::WorkerLoop()
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
			// A throwing task must not kill the host -- a buggy plugin stage should
			// be isolated. Report (non-fatal) and keep the worker serving.
			ReportError(E.what());
		}
		catch (...)
		{
			ReportError("Unknown exception in thread-pool worker");
		}
		{
			std::lock_guard Lock(Mutex);
			PendingCount -= 1;
			if (PendingCount == 0)
			{
				CondVar.notify_all();
			}
		}
	}
}

} // namespace Maho
