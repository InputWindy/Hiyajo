#pragma once

#include <WorldAdapter/Core/Backend.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

namespace Maho
{

struct FStubWorldBackendConfig
{
	std::size_t IdempotencyCapacity = WorldAdapterDefaultIdempotencyCapacity;
	std::chrono::milliseconds ExecutionDelay = std::chrono::milliseconds(0);
};

// Harness-only and test-only in-memory state. This is not FWorld or a production entity model.
class FStubWorldBackend final : public IWorldAdapterBackend
{
public:
	explicit FStubWorldBackend(FStubWorldBackendConfig Config = {});
	~FStubWorldBackend() override;

	FStubWorldBackend(const FStubWorldBackend&) = delete;
	FStubWorldBackend& operator=(const FStubWorldBackend&) = delete;

	[[nodiscard]] FWorldAdapterCapabilities GetCapabilities() const noexcept override;
	[[nodiscard]] FWorldAdapterSnapshotResponse GetSnapshot(const FWorldAdapterSnapshotRequest& Request) noexcept override;
	[[nodiscard]] FWorldAdapterExecuteResponse Execute(const FWorldAdapterExecuteRequest& Request) noexcept override;
	[[nodiscard]] FWorldAdapterUndoResponse Undo(const FWorldAdapterUndoRequest& Request) noexcept override;

	void BeginShutdown() noexcept override;
	void Shutdown() noexcept override;

	[[nodiscard]] std::uint64_t GetRevision(const std::string& SessionId, const std::string& WorldId) const noexcept;
	[[nodiscard]] std::size_t GetEntityCount(const std::string& SessionId, const std::string& WorldId) const noexcept;
	[[nodiscard]] std::uint64_t GetExecutionCount() const noexcept;
	[[nodiscard]] std::thread::id GetLastExecutionThreadId() const noexcept;

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

} // namespace Maho
