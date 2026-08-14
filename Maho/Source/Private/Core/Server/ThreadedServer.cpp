#include <Core/Server/ThreadedServer.h>

#include <Core/Misc/Log.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace Maho
{

namespace
{

struct FContextSlot
{
	std::unique_ptr<FTaskContext> Context;
	std::uint32_t Generation = 1;
	bool bOccupied = false;
};

} // namespace

struct FThreadedServer::FImpl
{
	std::thread Worker;
	mutable std::mutex Mutex;
	std::condition_variable WorkCv;
	std::condition_variable IdleCv;
	std::queue<std::unique_ptr<FServerTask>> Queue;
	std::vector<FContextSlot> Contexts;
	bool bRunning = false;
	bool bStopRequested = false;
	std::uint32_t PendingCount = 0;
	const char* ThreadName = "MahoServerThread";

	void ThreadMain(FThreadedServer& Owner)
	{
		MAHO_CORE_INFO("{} thread online", ThreadName);

		for (;;)
		{
			std::unique_ptr<FServerTask> Task;
			{
				std::unique_lock<std::mutex> Lock(Mutex);
				WorkCv.wait(Lock, [this]()
				{
					return bStopRequested || !Queue.empty();
				});

				if (bStopRequested && Queue.empty())
				{
					break;
				}

				Task = std::move(Queue.front());
				Queue.pop();
			}

			if (Task)
			{
				const FTaskContextId ContextId = Task->GetContextId();
				Task->Execute(Owner);
				if (ContextId.IsValid())
				{
					Owner.FreeContext(ContextId);
				}
			}

			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (PendingCount > 0)
				{
					--PendingCount;
				}
				if (PendingCount == 0)
				{
					IdleCv.notify_all();
				}
			}
		}

		MAHO_CORE_INFO("{} thread exiting", ThreadName);
	}

	void ClearContexts()
	{
		Contexts.clear();
	}
};

FThreadedServer::FThreadedServer()
	: Impl(std::make_unique<FImpl>())
{
}

FThreadedServer::~FThreadedServer()
{
	Shutdown();
}

bool FThreadedServer::Initialize()
{
	if (bInitialized)
	{
		return true;
	}

	CachedThreadName = GetServerThreadName();
	CachedLogName = GetServerLogName();
	if (!CachedThreadName)
	{
		CachedThreadName = "MahoServerThread";
	}
	if (!CachedLogName)
	{
		CachedLogName = "ThreadedServer";
	}

	Impl->ThreadName = CachedThreadName;
	Impl->bStopRequested = false;
	Impl->bRunning = true;
	Impl->PendingCount = 0;
	Impl->ClearContexts();

	Impl->Worker = std::thread([this]()
	{
		Impl->ThreadMain(*this);
	});

	MAHO_CORE_INFO("{} started ({})", CachedLogName, CachedThreadName);

	if (!OnInitialize())
	{
		MAHO_CORE_ERROR("{} OnInitialize failed", CachedLogName);
		Shutdown();
		return false;
	}

	bInitialized = true;
	MAHO_CORE_INFO("{} initialized", CachedLogName);
	return true;
}

void FThreadedServer::Shutdown()
{
	if (!Impl)
	{
		return;
	}

	const char* LogName = CachedLogName ? CachedLogName : "ThreadedServer";

	if (bInitialized)
	{
		Flush();
		OnShutdown();
	}

	if (Impl->bRunning)
	{
		{
			std::lock_guard<std::mutex> Lock(Impl->Mutex);
			Impl->bStopRequested = true;
		}
		Impl->WorkCv.notify_all();

		if (Impl->Worker.joinable())
		{
			Impl->Worker.join();
		}

		Impl->bRunning = false;
		MAHO_CORE_INFO("{} stopped", LogName);
	}

	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		Impl->ClearContexts();
	}

	bInitialized = false;
}

bool FThreadedServer::IsRunning() const
{
	return Impl && Impl->bRunning;
}

FTaskContextId FThreadedServer::AllocContext(std::unique_ptr<FTaskContext> InContext)
{
	FTaskContextId Id{};
	if (!Impl || !InContext)
	{
		return Id;
	}

	std::lock_guard<std::mutex> Lock(Impl->Mutex);

	std::uint32_t SlotIndex = static_cast<std::uint32_t>(Impl->Contexts.size());
	for (std::uint32_t Index = 0; Index < Impl->Contexts.size(); ++Index)
	{
		if (!Impl->Contexts[Index].bOccupied)
		{
			SlotIndex = Index;
			break;
		}
	}

	if (SlotIndex == Impl->Contexts.size())
	{
		Impl->Contexts.emplace_back();
	}

	FContextSlot& Slot = Impl->Contexts[SlotIndex];
	if (Slot.Generation == 0)
	{
		Slot.Generation = 1;
	}
	Slot.Context = std::move(InContext);
	Slot.bOccupied = true;

	Id.Index = SlotIndex;
	Id.Generation = Slot.Generation;
	return Id;
}

FTaskContext* FThreadedServer::GetContext(FTaskContextId Id) const
{
	if (!Impl || !Id.IsValid())
	{
		return nullptr;
	}

	std::lock_guard<std::mutex> Lock(Impl->Mutex);
	if (Id.Index >= Impl->Contexts.size())
	{
		return nullptr;
	}

	const FContextSlot& Slot = Impl->Contexts[Id.Index];
	if (!Slot.bOccupied || Slot.Generation != Id.Generation)
	{
		return nullptr;
	}
	return Slot.Context.get();
}

void FThreadedServer::FreeContext(FTaskContextId Id)
{
	if (!Impl || !Id.IsValid())
	{
		return;
	}

	std::lock_guard<std::mutex> Lock(Impl->Mutex);
	if (Id.Index >= Impl->Contexts.size())
	{
		return;
	}

	FContextSlot& Slot = Impl->Contexts[Id.Index];
	if (!Slot.bOccupied || Slot.Generation != Id.Generation)
	{
		return;
	}

	Slot.Context.reset();
	Slot.bOccupied = false;
	++Slot.Generation;
	if (Slot.Generation == 0)
	{
		Slot.Generation = 1;
	}
}

void FThreadedServer::Enqueue(std::unique_ptr<FServerTask> Task)
{
	if (!Task || !Impl)
	{
		return;
	}

	const char* LogName = CachedLogName ? CachedLogName : "ThreadedServer";

	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		if (!Impl->bRunning || Impl->bStopRequested)
		{
			MAHO_CORE_WARN("{}::Enqueue ignored (server not running)", LogName);
			return;
		}
		Impl->Queue.push(std::move(Task));
		++Impl->PendingCount;
	}
	Impl->WorkCv.notify_one();
}

void FThreadedServer::Enqueue(std::function<void(FThreadedServer&)> Function)
{
	if (!Function)
	{
		return;
	}
	Enqueue(std::make_unique<FLambdaServerTask>(std::move(Function)));
}

void FThreadedServer::Flush()
{
	if (!Impl || !Impl->bRunning)
	{
		return;
	}

	std::unique_lock<std::mutex> Lock(Impl->Mutex);
	Impl->IdleCv.wait(Lock, [this]()
	{
		return Impl->PendingCount == 0 || Impl->bStopRequested;
	});
}

} // namespace Maho
