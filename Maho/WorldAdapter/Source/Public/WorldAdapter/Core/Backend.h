#pragma once

#include <WorldAdapter/Protocol/Dto.h>

namespace Maho
{

class IWorldAdapterBackend
{
public:
	virtual ~IWorldAdapterBackend() = default;

	[[nodiscard]] virtual FWorldAdapterCapabilities GetCapabilities() const noexcept = 0;
	[[nodiscard]] virtual FWorldAdapterSnapshotResponse GetSnapshot(const FWorldAdapterSnapshotRequest& Request) noexcept = 0;
	[[nodiscard]] virtual FWorldAdapterExecuteResponse Execute(const FWorldAdapterExecuteRequest& Request) noexcept = 0;
	[[nodiscard]] virtual FWorldAdapterUndoResponse Undo(const FWorldAdapterUndoRequest& Request) noexcept = 0;

	virtual void BeginShutdown() noexcept = 0;
	virtual void Shutdown() noexcept = 0;
};

} // namespace Maho
