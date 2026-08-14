#pragma once

#include <WorldAdapter/Protocol/Dto.h>

#include <string>
#include <string_view>

namespace Maho
{

class FWorldAdapterJsonProtocol
{
public:
	[[nodiscard]] static FWorldAdapterCapabilities MinimalCapabilities();

	[[nodiscard]] static bool ParseSnapshotRequest(
		std::string_view Text,
		FWorldAdapterSnapshotRequest& OutRequest,
		FWorldAdapterError& OutError) noexcept;

	[[nodiscard]] static bool ParseExecuteRequest(
		std::string_view Text,
		FWorldAdapterExecuteRequest& OutRequest,
		FWorldAdapterError& OutError) noexcept;

	[[nodiscard]] static bool ParseUndoRequest(
		std::string_view Text,
		FWorldAdapterUndoRequest& OutRequest,
		FWorldAdapterError& OutError) noexcept;

	[[nodiscard]] static bool ValidateHealthResponse(
		const FWorldAdapterJson& Value,
		FWorldAdapterHealthResponse* OutResponse,
		FWorldAdapterError& OutError) noexcept;

	[[nodiscard]] static bool ValidateSnapshotResponse(
		const FWorldAdapterJson& Value,
		const FWorldAdapterSnapshotRequest* Request,
		FWorldAdapterSnapshotResponse* OutResponse,
		FWorldAdapterError& OutError) noexcept;

	[[nodiscard]] static bool ValidateExecuteResponse(
		const FWorldAdapterJson& Value,
		const FWorldAdapterExecuteRequest* Request,
		FWorldAdapterExecuteResponse* OutResponse,
		FWorldAdapterError& OutError) noexcept;

	[[nodiscard]] static bool ValidateUndoResponse(
		const FWorldAdapterJson& Value,
		const FWorldAdapterUndoRequest* Request,
		FWorldAdapterUndoResponse* OutResponse,
		FWorldAdapterError& OutError) noexcept;

	[[nodiscard]] static bool ParseJson(
		std::string_view Text,
		FWorldAdapterJson& OutValue,
		FWorldAdapterError& OutError,
		std::size_t SizeLimit = WorldAdapterRequestBodyLimit) noexcept;

	[[nodiscard]] static FWorldAdapterJson ToJson(const FWorldAdapterCapabilities& Value);
	[[nodiscard]] static FWorldAdapterJson ToJson(const FWorldAdapterError& Value);
	[[nodiscard]] static FWorldAdapterJson ToJson(const FWorldAdapterEntity& Value);
	[[nodiscard]] static FWorldAdapterJson ToJson(const FWorldAdapterHealthResponse& Value);
	[[nodiscard]] static FWorldAdapterJson ToJson(const FWorldAdapterSnapshotResponse& Value);
	[[nodiscard]] static FWorldAdapterJson ToJson(const FWorldAdapterExecuteResponse& Value);
	[[nodiscard]] static FWorldAdapterJson ToJson(const FWorldAdapterUndoResponse& Value);

	[[nodiscard]] static std::string Serialize(const FWorldAdapterJson& Value);
	[[nodiscard]] static std::string CanonicalFingerprint(const FWorldAdapterJson& Value) noexcept;
	[[nodiscard]] static bool IsUuid(std::string_view Value) noexcept;
};

} // namespace Maho
