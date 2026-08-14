#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Maho
{

using FWorldAdapterJson = nlohmann::json;

inline constexpr const char* WorldAdapterProtocolVersion = "1.0";
inline constexpr std::uint64_t WorldAdapterMaxSafeInteger = 9007199254740991ULL;
inline constexpr std::size_t WorldAdapterRequestBodyLimit = 1024U * 1024U;
inline constexpr std::size_t WorldAdapterResponseBodyLimit = 4U * 1024U * 1024U;
inline constexpr std::size_t WorldAdapterDefaultQueueCapacity = 64;
inline constexpr std::size_t WorldAdapterDefaultIdempotencyCapacity = 1024;

struct FWorldAdapterError
{
	std::string Code;
	std::string Message;
	FWorldAdapterJson Details = FWorldAdapterJson::object();
	bool bRetryable = false;
};

struct FWorldAdapterCapabilities
{
	bool bSupportsAtomicTransactions = false;
	bool bSupportsDryRun = false;
	bool bSupportsUndo = false;
	bool bSupportsIdempotency = true;
	std::uint32_t MaxToolCalls = 1;
	std::vector<std::string> SupportedTools;
};

struct FWorldAdapterTransform
{
	std::array<double, 3> Position = { 0.0, 0.0, 0.0 };
	std::array<double, 3> Rotation = { 0.0, 0.0, 0.0 };
	std::array<double, 3> Scale = { 1.0, 1.0, 1.0 };
};

struct FWorldAdapterTransformPatch
{
	std::optional<std::array<double, 3>> Position;
	std::optional<std::array<double, 3>> Rotation;
	std::optional<std::array<double, 3>> Scale;
};

struct FWorldAdapterEntityProperties
{
	std::array<double, 4> Color = { 1.0, 1.0, 1.0, 1.0 };
	bool bVisible = true;
	std::string Label;
};

struct FWorldAdapterEntity
{
	std::string EntityId;
	std::uint64_t Generation = 1;
	std::string Name;
	std::string EntityType = "primitive";
	std::string PrimitiveType;
	FWorldAdapterTransform Transform;
	FWorldAdapterEntityProperties Properties;
};

struct FWorldAdapterToolCall
{
	std::string ToolCallId;
	std::string ToolName;
	FWorldAdapterJson Args = FWorldAdapterJson::object();
};

struct FWorldAdapterChange
{
	std::string Operation;
	std::optional<std::string> EntityId;
	std::optional<std::string> PropertyName;
	FWorldAdapterJson Before;
	FWorldAdapterJson After;
};

struct FWorldAdapterToolResult
{
	bool bOk = false;
	std::string RequestId;
	std::optional<std::string> ToolCallId;
	std::uint64_t BeforeRevision = 0;
	std::uint64_t AfterRevision = 0;
	std::vector<FWorldAdapterChange> Changes;
	std::optional<std::string> UndoToken;
	std::optional<FWorldAdapterError> Error;
	FWorldAdapterJson Data;
	bool bDryRun = false;
	bool bRolledBack = false;
};

struct FWorldAdapterHealthResponse
{
	bool bOk = true;
	std::string ServerName = "maho-world-adapter-harness";
	std::string ServerVersion = "0.4.2";
	FWorldAdapterCapabilities Capabilities;
	std::optional<FWorldAdapterError> Error;
};

struct FWorldAdapterSnapshotRequest
{
	std::string RequestId;
	std::string SessionId;
	std::string WorldId;
};

struct FWorldAdapterSnapshotResponse
{
	bool bOk = true;
	std::string RequestId;
	std::string SessionId;
	std::string WorldId;
	std::uint64_t WorldRevision = 0;
	std::uint64_t TimestampMs = 0;
	FWorldAdapterCapabilities Capabilities;
	std::vector<FWorldAdapterEntity> Entities;
	std::vector<FWorldAdapterJson> History;
	std::optional<FWorldAdapterError> Error;
};

struct FWorldAdapterExecuteRequest
{
	std::string RequestId;
	std::string SessionId;
	std::string WorldId;
	std::uint64_t ExpectedRevision = 0;
	bool bDryRun = false;
	bool bAtomic = false;
	std::vector<FWorldAdapterToolCall> ToolCalls;
	FWorldAdapterJson CanonicalPayload;
};

struct FWorldAdapterExecuteResponse
{
	bool bOk = false;
	std::string RequestId;
	std::string SessionId;
	std::string WorldId;
	std::uint64_t BeforeRevision = 0;
	std::uint64_t AfterRevision = 0;
	bool bReplayed = false;
	std::vector<FWorldAdapterToolResult> ToolResults;
	std::vector<FWorldAdapterChange> Changes;
	std::optional<std::string> UndoToken;
	std::optional<FWorldAdapterError> Error;
	std::optional<std::size_t> FailedToolCallIndex;
};

struct FWorldAdapterUndoRequest
{
	std::string RequestId;
	std::string SessionId;
	std::string WorldId;
	std::uint64_t ExpectedRevision = 0;
	std::optional<std::string> UndoToken;
	FWorldAdapterJson CanonicalPayload;
};

struct FWorldAdapterUndoResponse
{
	bool bOk = false;
	std::string RequestId;
	std::string SessionId;
	std::string WorldId;
	std::uint64_t BeforeRevision = 0;
	std::uint64_t AfterRevision = 0;
	bool bReplayed = false;
	std::vector<FWorldAdapterChange> Changes;
	std::optional<std::string> UndoToken;
	std::optional<FWorldAdapterError> Error;
	FWorldAdapterJson Data;
};

struct FWorldAdapterIdempotencyEntry
{
	std::string SessionId;
	std::string WorldId;
	std::string RequestId;
	std::string Operation;
	std::string PayloadFingerprint;
	FWorldAdapterExecuteResponse ExecuteResponse;
};

} // namespace Maho
