#pragma once

#include <WorldAdapter/Core/Backend.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>

namespace Maho
{

enum class EWorldAdapterRequestState : std::uint8_t
{
	Queued = 0,
	Executing,
	Completed,
	TimedOut,
	Cancelled,
};

enum class EWorldAdapterWaitResult : std::uint8_t
{
	Completed = 0,
	TimedOut,
	Cancelled,
};

enum class EWorldAdapterEnqueueResult : std::uint8_t
{
	Accepted = 0,
	Full,
	ShuttingDown,
};

using FWorldAdapterCommandRequest = std::variant<
	FWorldAdapterSnapshotRequest,
	FWorldAdapterExecuteRequest,
	FWorldAdapterUndoRequest>;

using FWorldAdapterCommandResponse = std::variant<
	FWorldAdapterSnapshotResponse,
	FWorldAdapterExecuteResponse,
	FWorldAdapterUndoResponse>;

struct FWorldAdapterCommandResult
{
	FWorldAdapterCommandResponse Response;
};

class FWorldAdapterRequestState
{
public:
	FWorldAdapterRequestState() = default;

	FWorldAdapterRequestState(const FWorldAdapterRequestState&) = delete;
	FWorldAdapterRequestState& operator=(const FWorldAdapterRequestState&) = delete;

	[[nodiscard]] bool TryMarkExecuting() noexcept;
	void Complete(FWorldAdapterCommandResult Result) noexcept;
	void Cancel() noexcept;

	[[nodiscard]] EWorldAdapterWaitResult WaitFor(
		std::chrono::milliseconds Timeout,
		FWorldAdapterCommandResult& OutResult) noexcept;

	[[nodiscard]] EWorldAdapterRequestState GetState() const noexcept;
	[[nodiscard]] bool WasExecutionStarted() const noexcept;

private:
	mutable std::mutex Mutex;
	std::condition_variable Condition;
	EWorldAdapterRequestState State = EWorldAdapterRequestState::Queued;
	bool bExecutionStarted = false;
	std::optional<FWorldAdapterCommandResult> Result;
};

struct FWorldAdapterCommandEnvelope
{
	FWorldAdapterCommandRequest Request;
	std::shared_ptr<FWorldAdapterRequestState> RequestState;
};

class FWorldAdapterCommandQueue
{
public:
	explicit FWorldAdapterCommandQueue(std::size_t Capacity = WorldAdapterDefaultQueueCapacity);
	~FWorldAdapterCommandQueue();

	FWorldAdapterCommandQueue(const FWorldAdapterCommandQueue&) = delete;
	FWorldAdapterCommandQueue& operator=(const FWorldAdapterCommandQueue&) = delete;

	[[nodiscard]] EWorldAdapterEnqueueResult Enqueue(FWorldAdapterCommandEnvelope Envelope) noexcept;
	[[nodiscard]] bool PumpOne(IWorldAdapterBackend& Backend) noexcept;
	[[nodiscard]] std::size_t Pump(IWorldAdapterBackend& Backend, std::size_t MaximumCommands) noexcept;

	void BeginShutdown() noexcept;

	[[nodiscard]] bool IsAccepting() const noexcept;
	[[nodiscard]] std::size_t GetSize() const noexcept;
	[[nodiscard]] std::size_t GetCapacity() const noexcept { return Capacity; }
	[[nodiscard]] std::thread::id GetPumpThreadId() const noexcept;

private:
	std::size_t Capacity;
	mutable std::mutex Mutex;
	std::deque<FWorldAdapterCommandEnvelope> Queue;
	bool bAccepting = true;
	std::thread::id PumpThreadId;
};

} // namespace Maho
