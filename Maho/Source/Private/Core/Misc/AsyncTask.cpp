#include <Core/Misc/AsyncTask.h>

#include <atomic>
#include <thread>
#include <utility>

namespace Maho
{

struct FAsyncTask::FImpl
{
	std::thread Thread;
	std::atomic<bool> bDone{true};
};

FAsyncTask::FAsyncTask()
	: Impl(std::make_unique<FImpl>())
{
}

FAsyncTask::~FAsyncTask()
{
	Wait();
}

FAsyncTask::FAsyncTask(FAsyncTask&& Other) noexcept
	: Impl(std::move(Other.Impl))
{
	Other.Impl = std::make_unique<FImpl>();
}

FAsyncTask& FAsyncTask::operator=(FAsyncTask&& Other) noexcept
{
	if (this != &Other)
	{
		Wait();
		Impl = std::move(Other.Impl);
		Other.Impl = std::make_unique<FImpl>();
	}
	return *this;
}

void FAsyncTask::Launch(std::function<void()> Work)
{
	if (!Impl || !Work)
	{
		return;
	}

	Wait();

	Impl->bDone.store(false, std::memory_order_release);
	Impl->Thread = std::thread([ImplPtr = Impl.get(), Work = std::move(Work)]() mutable
	{
		Work();
		ImplPtr->bDone.store(true, std::memory_order_release);
	});
}

void FAsyncTask::Wait()
{
	if (!Impl)
	{
		return;
	}

	if (Impl->Thread.joinable())
	{
		Impl->Thread.join();
	}
}

bool FAsyncTask::IsDone() const
{
	return !Impl || Impl->bDone.load(std::memory_order_acquire);
}

bool FAsyncTask::IsRunning() const
{
	return Impl && Impl->Thread.joinable() && !Impl->bDone.load(std::memory_order_acquire);
}

std::unique_ptr<FAsyncTask> FAsyncTask::LaunchNew(std::function<void()> Work)
{
	auto Task = std::make_unique<FAsyncTask>();
	Task->Launch(std::move(Work));
	return Task;
}

} // namespace Maho
