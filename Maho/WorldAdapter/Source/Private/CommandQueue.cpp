#include <WorldAdapter/Core/CommandQueue.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace Maho
{

namespace
{

FWorldAdapterError InternalError()
{
	FWorldAdapterError Error;
	Error.Code = "INTERNAL_ERROR";
	Error.Message = "World adapter backend failed safely";
	Error.bRetryable = true;
	return Error;
}

FWorldAdapterCommandResult MakeInternalFailure(const FWorldAdapterCommandRequest& Request)
{
	return std::visit([](const auto& TypedRequest) -> FWorldAdapterCommandResult
	{
		using TRequest = std::decay_t<decltype(TypedRequest)>;
		if constexpr (std::is_same_v<TRequest, FWorldAdapterSnapshotRequest>)
		{
			FWorldAdapterSnapshotResponse Response;
			Response.bOk = false;
			Response.RequestId = TypedRequest.RequestId;
			Response.SessionId = TypedRequest.SessionId;
			Response.WorldId = TypedRequest.WorldId;
			Response.Error = InternalError();
			return { std::move(Response) };
		}
		else if constexpr (std::is_same_v<TRequest, FWorldAdapterExecuteRequest>)
		{
			FWorldAdapterExecuteResponse Response;
			Response.RequestId = TypedRequest.RequestId;
			Response.SessionId = TypedRequest.SessionId;
			Response.WorldId = TypedRequest.WorldId;
			Response.Error = InternalError();
			return { std::move(Response) };
		}
		else
		{
			FWorldAdapterUndoResponse Response;
			Response.RequestId = TypedRequest.RequestId;
			Response.SessionId = TypedRequest.SessionId;
			Response.WorldId = TypedRequest.WorldId;
			Response.Error = InternalError();
			return { std::move(Response) };
		}
	}, Request);
}

} // namespace

bool FWorldAdapterRequestState::TryMarkExecuting() noexcept
{
	try
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (State != EWorldAdapterRequestState::Queued)
		{
			return false;
		}
		State = EWorldAdapterRequestState::Executing;
		bExecutionStarted = true;
		return true;
	}
	catch (...)
	{
		return false;
	}
}

void FWorldAdapterRequestState::Complete(FWorldAdapterCommandResult InResult) noexcept
{
	try
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			if (!bExecutionStarted || State == EWorldAdapterRequestState::Cancelled || State == EWorldAdapterRequestState::Completed)
			{
				return;
			}
			Result = std::move(InResult);
			State = EWorldAdapterRequestState::Completed;
		}
		Condition.notify_all();
	}
	catch (...)
	{
		Condition.notify_all();
	}
}

void FWorldAdapterRequestState::Cancel() noexcept
{
	try
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			if (State != EWorldAdapterRequestState::Queued)
			{
				return;
			}
			State = EWorldAdapterRequestState::Cancelled;
		}
		Condition.notify_all();
	}
	catch (...)
	{
		Condition.notify_all();
	}
}

EWorldAdapterWaitResult FWorldAdapterRequestState::WaitFor(
	std::chrono::milliseconds Timeout,
	FWorldAdapterCommandResult& OutResult) noexcept
{
	try
	{
		std::unique_lock<std::mutex> Lock(Mutex);
		const bool bSignalled = Condition.wait_for(Lock, Timeout, [this]()
		{
			return State == EWorldAdapterRequestState::Completed ||
				State == EWorldAdapterRequestState::TimedOut ||
				State == EWorldAdapterRequestState::Cancelled;
		});
		if (!bSignalled && (State == EWorldAdapterRequestState::Queued || State == EWorldAdapterRequestState::Executing))
		{
			State = EWorldAdapterRequestState::TimedOut;
		}
		if (State == EWorldAdapterRequestState::Completed && Result)
		{
			OutResult = *Result;
			return EWorldAdapterWaitResult::Completed;
		}
		if (State == EWorldAdapterRequestState::Cancelled)
		{
			return EWorldAdapterWaitResult::Cancelled;
		}
		return EWorldAdapterWaitResult::TimedOut;
	}
	catch (...)
	{
		return EWorldAdapterWaitResult::Cancelled;
	}
}

EWorldAdapterRequestState FWorldAdapterRequestState::GetState() const noexcept
{
	try
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return State;
	}
	catch (...)
	{
		return EWorldAdapterRequestState::Cancelled;
	}
}

bool FWorldAdapterRequestState::WasExecutionStarted() const noexcept
{
	try
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return bExecutionStarted;
	}
	catch (...)
	{
		return false;
	}
}

FWorldAdapterCommandQueue::FWorldAdapterCommandQueue(std::size_t InCapacity)
	: Capacity((std::max)(std::size_t(1), InCapacity))
{
}

FWorldAdapterCommandQueue::~FWorldAdapterCommandQueue()
{
	BeginShutdown();
}

EWorldAdapterEnqueueResult FWorldAdapterCommandQueue::Enqueue(FWorldAdapterCommandEnvelope Envelope) noexcept
{
	try
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (!bAccepting)
		{
			return EWorldAdapterEnqueueResult::ShuttingDown;
		}
		if (Queue.size() >= Capacity)
		{
			return EWorldAdapterEnqueueResult::Full;
		}
		if (!Envelope.RequestState)
		{
			return EWorldAdapterEnqueueResult::ShuttingDown;
		}
		Queue.push_back(std::move(Envelope));
		return EWorldAdapterEnqueueResult::Accepted;
	}
	catch (...)
	{
		return EWorldAdapterEnqueueResult::ShuttingDown;
	}
}

bool FWorldAdapterCommandQueue::PumpOne(IWorldAdapterBackend& Backend) noexcept
{
	FWorldAdapterCommandEnvelope Envelope;
	try
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			const std::thread::id CurrentThread = std::this_thread::get_id();
			if (PumpThreadId == std::thread::id())
			{
				PumpThreadId = CurrentThread;
			}
			else if (PumpThreadId != CurrentThread)
			{
				return false;
			}
			if (Queue.empty())
			{
				return false;
			}
			Envelope = std::move(Queue.front());
			Queue.pop_front();
		}

		if (!Envelope.RequestState || !Envelope.RequestState->TryMarkExecuting())
		{
			return true;
		}

		FWorldAdapterCommandResult Result = std::visit([&Backend](const auto& Request) -> FWorldAdapterCommandResult
		{
			using TRequest = std::decay_t<decltype(Request)>;
			if constexpr (std::is_same_v<TRequest, FWorldAdapterSnapshotRequest>)
			{
				return { Backend.GetSnapshot(Request) };
			}
			else if constexpr (std::is_same_v<TRequest, FWorldAdapterExecuteRequest>)
			{
				return { Backend.Execute(Request) };
			}
			else
			{
				return { Backend.Undo(Request) };
			}
		}, Envelope.Request);
		Envelope.RequestState->Complete(std::move(Result));
		return true;
	}
	catch (...)
	{
		if (Envelope.RequestState)
		{
			Envelope.RequestState->Complete(MakeInternalFailure(Envelope.Request));
		}
		return true;
	}
}

std::size_t FWorldAdapterCommandQueue::Pump(IWorldAdapterBackend& Backend, std::size_t MaximumCommands) noexcept
{
	std::size_t Count = 0;
	while (Count < MaximumCommands && PumpOne(Backend))
	{
		++Count;
	}
	return Count;
}

void FWorldAdapterCommandQueue::BeginShutdown() noexcept
{
	try
	{
		std::deque<FWorldAdapterCommandEnvelope> Pending;
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			if (!bAccepting && Queue.empty())
			{
				return;
			}
			bAccepting = false;
			Pending.swap(Queue);
		}
		for (FWorldAdapterCommandEnvelope& Envelope : Pending)
		{
			if (Envelope.RequestState)
			{
				Envelope.RequestState->Cancel();
			}
		}
	}
	catch (...)
	{
	}
}

bool FWorldAdapterCommandQueue::IsAccepting() const noexcept
{
	try
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return bAccepting;
	}
	catch (...)
	{
		return false;
	}
}

std::size_t FWorldAdapterCommandQueue::GetSize() const noexcept
{
	try
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return Queue.size();
	}
	catch (...)
	{
		return 0;
	}
}

std::thread::id FWorldAdapterCommandQueue::GetPumpThreadId() const noexcept
{
	try
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return PumpThreadId;
	}
	catch (...)
	{
		return {};
	}
}

} // namespace Maho
