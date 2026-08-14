#include <WorldAdapter/Stub/StubBackend.h>

#include <WorldAdapter/Protocol/Json.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <list>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Maho
{

namespace
{

struct FStubWorldState
{
	std::uint64_t Revision = 0;
	std::map<std::string, FWorldAdapterEntity> Entities;
};

struct FCacheRecord
{
	std::string Key;
	std::string Fingerprint;
	FWorldAdapterExecuteResponse Response;
};

std::string ScopeKey(const std::string& SessionId, const std::string& WorldId)
{
	return std::to_string(SessionId.size()) + ":" + SessionId +
		std::to_string(WorldId.size()) + ":" + WorldId;
}

std::string CacheKey(const FWorldAdapterExecuteRequest& Request)
{
	return ScopeKey(Request.SessionId, Request.WorldId) +
		std::to_string(Request.RequestId.size()) + ":" + Request.RequestId +
		"7:execute";
}

FWorldAdapterError MakeError(
	std::string Code,
	std::string Message,
	FWorldAdapterJson Details = FWorldAdapterJson::object(),
	bool bRetryable = false)
{
	FWorldAdapterError Error;
	Error.Code = std::move(Code);
	Error.Message = std::move(Message);
	Error.Details = std::move(Details);
	Error.bRetryable = bRetryable;
	return Error;
}

FWorldAdapterExecuteResponse MakeExecuteFailure(
	const FWorldAdapterExecuteRequest& Request,
	std::uint64_t Revision,
	FWorldAdapterError Error)
{
	FWorldAdapterExecuteResponse Response;
	Response.RequestId = Request.RequestId;
	Response.SessionId = Request.SessionId;
	Response.WorldId = Request.WorldId;
	Response.BeforeRevision = Revision;
	Response.AfterRevision = Revision;
	Response.Error = Error;
	Response.FailedToolCallIndex = 0;
	if (!Request.ToolCalls.empty())
	{
		FWorldAdapterToolResult ToolResult;
		ToolResult.RequestId = Request.RequestId;
		ToolResult.ToolCallId = Request.ToolCalls.front().ToolCallId;
		ToolResult.BeforeRevision = Revision;
		ToolResult.AfterRevision = Revision;
		ToolResult.Error = std::move(Error);
		ToolResult.Data = nullptr;
		ToolResult.bDryRun = Request.bDryRun;
		Response.ToolResults.push_back(std::move(ToolResult));
	}
	return Response;
}

bool IsPrimitiveType(const std::string& Value)
{
	return Value == "cube" || Value == "sphere" || Value == "cylinder" || Value == "plane";
}

bool IsFiniteVector(const FWorldAdapterJson& Value, std::size_t Size)
{
	if (!Value.is_array() || Value.size() != Size)
	{
		return false;
	}
	for (const FWorldAdapterJson& Entry : Value)
	{
		if (!Entry.is_number() || !std::isfinite(Entry.get<double>()))
		{
			return false;
		}
	}
	return true;
}

bool ApplyTransformPatch(const FWorldAdapterJson& Value, FWorldAdapterTransform& Transform, bool bRequireOne = true)
{
	if (!Value.is_object() || (bRequireOne && Value.empty()))
	{
		return false;
	}
	for (auto It = Value.begin(); It != Value.end(); ++It)
	{
		if (It.key() != "position" && It.key() != "rotation" && It.key() != "scale")
		{
			return false;
		}
	}
	auto Read = [&Value](const char* Name, std::array<double, 3>& Target, double Minimum, double Maximum, bool bExclusiveMinimum)
	{
		if (!Value.contains(Name))
		{
			return true;
		}
		const FWorldAdapterJson& Vector = Value.at(Name);
		if (!IsFiniteVector(Vector, 3))
		{
			return false;
		}
		for (std::size_t Index = 0; Index < 3; ++Index)
		{
			const double Number = Vector[Index].get<double>();
			if ((bExclusiveMinimum ? Number <= Minimum : Number < Minimum) || Number > Maximum)
			{
				return false;
			}
			Target[Index] = Number;
		}
		return true;
	};
	return Read("position", Transform.Position, -100000.0, 100000.0, false) &&
		Read("rotation", Transform.Rotation, -360000.0, 360000.0, false) &&
		Read("scale", Transform.Scale, 0.0001, 10000.0, true);
}

bool ApplyProperties(const FWorldAdapterJson& Value, FWorldAdapterEntityProperties& Properties)
{
	if (!Value.is_object())
	{
		return false;
	}
	for (auto It = Value.begin(); It != Value.end(); ++It)
	{
		if (It.key() != "color" && It.key() != "visible" && It.key() != "label")
		{
			return false;
		}
	}
	if (Value.contains("color"))
	{
		const FWorldAdapterJson& Color = Value.at("color");
		if (!IsFiniteVector(Color, 4))
		{
			return false;
		}
		for (std::size_t Index = 0; Index < 4; ++Index)
		{
			const double Number = Color[Index].get<double>();
			if (Number < 0.0 || Number > 1.0)
			{
				return false;
			}
			Properties.Color[Index] = Number;
		}
	}
	if (Value.contains("visible"))
	{
		if (!Value.at("visible").is_boolean())
		{
			return false;
		}
		Properties.bVisible = Value.at("visible").get<bool>();
	}
	if (Value.contains("label"))
	{
		if (!Value.at("label").is_string())
		{
			return false;
		}
		Properties.Label = Value.at("label").get<std::string>();
		if (Properties.Label.size() > 256)
		{
			return false;
		}
	}
	return true;
}

FWorldAdapterJson TransformJson(const FWorldAdapterTransform& Transform)
{
	return {
		{ "position", Transform.Position },
		{ "rotation", Transform.Rotation },
		{ "scale", Transform.Scale },
	};
}

std::uint64_t CurrentTimestampMs()
{
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

struct FStubWorldBackend::FImpl
{
	explicit FImpl(FStubWorldBackendConfig InConfig)
		: Config(std::move(InConfig))
		, RandomEngine(MakeSeed())
	{
		Config.IdempotencyCapacity = (std::max)(std::size_t(1), Config.IdempotencyCapacity);
	}

	static std::uint64_t MakeSeed()
	{
		std::random_device Device;
		const std::uint64_t Time = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
		return (static_cast<std::uint64_t>(Device()) << 32U) ^ Device() ^ Time;
	}

	std::string NewUuid()
	{
		for (;;)
		{
			std::array<std::uint8_t, 16> Bytes{};
			for (std::size_t Offset = 0; Offset < Bytes.size(); Offset += 8)
			{
				const std::uint64_t Block = RandomEngine();
				for (std::size_t Byte = 0; Byte < 8; ++Byte)
				{
					Bytes[Offset + Byte] = static_cast<std::uint8_t>((Block >> (Byte * 8U)) & 0xffU);
				}
			}
			Bytes[6] = static_cast<std::uint8_t>((Bytes[6] & 0x0fU) | 0x40U);
			Bytes[8] = static_cast<std::uint8_t>((Bytes[8] & 0x3fU) | 0x80U);
			std::ostringstream Stream;
			Stream << std::hex << std::setfill('0');
			for (std::size_t Index = 0; Index < Bytes.size(); ++Index)
			{
				if (Index == 4 || Index == 6 || Index == 8 || Index == 10)
				{
					Stream << '-';
				}
				Stream << std::setw(2) << static_cast<unsigned int>(Bytes[Index]);
			}
			std::string Result = Stream.str();
			if (GeneratedIds.emplace(Result).second)
			{
				return Result;
			}
		}
	}

	FStubWorldState& GetWorld(const std::string& SessionId, const std::string& WorldId)
	{
		return Worlds[ScopeKey(SessionId, WorldId)];
	}

	const FStubWorldState* FindWorld(const std::string& SessionId, const std::string& WorldId) const
	{
		const auto It = Worlds.find(ScopeKey(SessionId, WorldId));
		return It == Worlds.end() ? nullptr : &It->second;
	}

	FWorldAdapterExecuteResponse* FindCached(const std::string& Key, const std::string& Fingerprint, bool& bConflict)
	{
		bConflict = false;
		const auto It = CacheByKey.find(Key);
		if (It == CacheByKey.end())
		{
			return nullptr;
		}
		if (It->second->Fingerprint != Fingerprint)
		{
			bConflict = true;
			return nullptr;
		}
		CacheLru.splice(CacheLru.begin(), CacheLru, It->second);
		It->second = CacheLru.begin();
		return &It->second->Response;
	}

	void StoreCached(std::string Key, std::string Fingerprint, const FWorldAdapterExecuteResponse& Response)
	{
		if (const auto Existing = CacheByKey.find(Key); Existing != CacheByKey.end())
		{
			CacheLru.erase(Existing->second);
			CacheByKey.erase(Existing);
		}
		CacheLru.push_front({ std::move(Key), std::move(Fingerprint), Response });
		CacheByKey[CacheLru.front().Key] = CacheLru.begin();
		while (CacheLru.size() > Config.IdempotencyCapacity)
		{
			CacheByKey.erase(CacheLru.back().Key);
			CacheLru.pop_back();
		}
	}

	FStubWorldBackendConfig Config;
	std::unordered_map<std::string, FStubWorldState> Worlds;
	std::list<FCacheRecord> CacheLru;
	std::unordered_map<std::string, std::list<FCacheRecord>::iterator> CacheByKey;
	std::unordered_set<std::string> GeneratedIds;
	std::mt19937_64 RandomEngine;
	std::uint64_t ExecutionCount = 0;
	std::thread::id LastExecutionThreadId;
	bool bShuttingDown = false;
	bool bShutdown = false;
};

FStubWorldBackend::FStubWorldBackend(FStubWorldBackendConfig Config)
	: Impl(std::make_unique<FImpl>(std::move(Config)))
{
}

FStubWorldBackend::~FStubWorldBackend()
{
	Shutdown();
}

FWorldAdapterCapabilities FStubWorldBackend::GetCapabilities() const noexcept
{
	return FWorldAdapterJsonProtocol::MinimalCapabilities();
}

FWorldAdapterSnapshotResponse FStubWorldBackend::GetSnapshot(const FWorldAdapterSnapshotRequest& Request) noexcept
{
	FWorldAdapterSnapshotResponse Response;
	Response.RequestId = Request.RequestId;
	Response.SessionId = Request.SessionId;
	Response.WorldId = Request.WorldId;
	Response.Capabilities = GetCapabilities();
	try
	{
		if (!Impl || Impl->bShutdown)
		{
			Response.bOk = false;
			Response.Error = MakeError("ADAPTER_UNAVAILABLE", "Stub backend is shut down", {}, true);
			return Response;
		}
		FStubWorldState& World = Impl->GetWorld(Request.SessionId, Request.WorldId);
		Response.WorldRevision = World.Revision;
		Response.TimestampMs = CurrentTimestampMs();
		for (const auto& Pair : World.Entities)
		{
			Response.Entities.push_back(Pair.second);
		}
		return Response;
	}
	catch (...)
	{
		Response.bOk = false;
		Response.Error = MakeError("INTERNAL_ERROR", "Stub snapshot failed safely", {}, true);
		return Response;
	}
}

FWorldAdapterExecuteResponse FStubWorldBackend::Execute(const FWorldAdapterExecuteRequest& Request) noexcept
{
	try
	{
		if (!Impl || Impl->bShutdown)
		{
			return MakeExecuteFailure(Request, 0, MakeError("ADAPTER_UNAVAILABLE", "Stub backend is shut down", {}, true));
		}
		FStubWorldState& World = Impl->GetWorld(Request.SessionId, Request.WorldId);
		const std::string Key = CacheKey(Request);
		const std::string Fingerprint = FWorldAdapterJsonProtocol::CanonicalFingerprint(Request.CanonicalPayload);
		bool bConflict = false;
		if (FWorldAdapterExecuteResponse* Cached = Impl->FindCached(Key, Fingerprint, bConflict))
		{
			FWorldAdapterExecuteResponse Replay = *Cached;
			Replay.bReplayed = true;
			return Replay;
		}
		if (bConflict)
		{
			return MakeExecuteFailure(
				Request,
				World.Revision,
				MakeError("REQUEST_ID_CONFLICT", "request_id was reused with a different payload"));
		}
		if (Request.ExpectedRevision != World.Revision)
		{
			FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(
				Request,
				World.Revision,
				MakeError(
					"REVISION_CONFLICT",
					"Expected revision " + std::to_string(Request.ExpectedRevision) + ", current revision is " + std::to_string(World.Revision),
					{
						{ "expected_revision", Request.ExpectedRevision },
						{ "current_revision", World.Revision },
					},
					true));
			Impl->StoreCached(Key, Fingerprint, Failure);
			return Failure;
		}
		if (Request.ToolCalls.size() != 1)
		{
			FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(
				Request,
				World.Revision,
				MakeError(
					"INVALID_REQUEST",
					"ToolCall count exceeds the world adapter capability",
					{ { "tool_call_count", Request.ToolCalls.size() }, { "max_tool_calls", 1 } }));
			Impl->StoreCached(Key, Fingerprint, Failure);
			return Failure;
		}
		if (Request.bDryRun)
		{
			FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(Request, World.Revision, MakeError("INVALID_REQUEST", "World adapter does not support dry-run execution", { { "field", "dry_run" } }));
			Impl->StoreCached(Key, Fingerprint, Failure);
			return Failure;
		}
		if (Request.bAtomic)
		{
			FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(Request, World.Revision, MakeError("INVALID_REQUEST", "World adapter does not support atomic transactions", { { "field", "atomic" } }));
			Impl->StoreCached(Key, Fingerprint, Failure);
			return Failure;
		}
		const FWorldAdapterToolCall& Call = Request.ToolCalls.front();
		if (Call.ToolName != "world.get_summary" && Call.ToolName != "entity.spawn_primitive" && Call.ToolName != "entity.set_transform")
		{
			FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(Request, World.Revision, MakeError("INVALID_REQUEST", "World adapter does not advertise the requested tool", { { "tool_name", Call.ToolName } }));
			Impl->StoreCached(Key, Fingerprint, Failure);
			return Failure;
		}

		if (Impl->Config.ExecutionDelay.count() > 0)
		{
			std::this_thread::sleep_for(Impl->Config.ExecutionDelay);
		}
		Impl->LastExecutionThreadId = std::this_thread::get_id();
		++Impl->ExecutionCount;

		const std::uint64_t BeforeRevision = World.Revision;
		FWorldAdapterExecuteResponse Response;
		Response.bOk = true;
		Response.RequestId = Request.RequestId;
		Response.SessionId = Request.SessionId;
		Response.WorldId = Request.WorldId;
		Response.BeforeRevision = BeforeRevision;
		FWorldAdapterToolResult ToolResult;
		ToolResult.bOk = true;
		ToolResult.RequestId = Request.RequestId;
		ToolResult.ToolCallId = Call.ToolCallId;
		ToolResult.BeforeRevision = BeforeRevision;
		ToolResult.bDryRun = false;

		if (Call.ToolName == "world.get_summary")
		{
			std::map<std::string, std::size_t> ByPrimitiveType;
			for (const auto& Pair : World.Entities)
			{
				++ByPrimitiveType[Pair.second.PrimitiveType];
			}
			ToolResult.Data = {
				{ "world_id", Request.WorldId },
				{ "revision", World.Revision },
				{ "entity_count", World.Entities.size() },
				{ "by_primitive_type", ByPrimitiveType },
			};
		}
		else if (Call.ToolName == "entity.spawn_primitive")
		{
			if (!Call.Args.is_object() || !Call.Args.contains("primitive_type") || !Call.Args.at("primitive_type").is_string())
			{
				FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(Request, World.Revision, MakeError("INVALID_ARGUMENT", "primitive_type is required"));
				Impl->StoreCached(Key, Fingerprint, Failure);
				return Failure;
			}
			FWorldAdapterEntity Entity;
			Entity.PrimitiveType = Call.Args.at("primitive_type").get<std::string>();
			if (!IsPrimitiveType(Entity.PrimitiveType))
			{
				FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(Request, World.Revision, MakeError("INVALID_ARGUMENT", "primitive_type is not supported"));
				Impl->StoreCached(Key, Fingerprint, Failure);
				return Failure;
			}
			Entity.EntityId = Impl->NewUuid();
			Entity.Generation = 1;
			Entity.Name = Call.Args.value("name", Entity.PrimitiveType + "_" + std::to_string(World.Entities.size() + 1));
			if (Entity.Name.empty() || Entity.Name.size() > 128 ||
				(Call.Args.contains("transform") && !ApplyTransformPatch(Call.Args.at("transform"), Entity.Transform, false)) ||
				(Call.Args.contains("properties") && !ApplyProperties(Call.Args.at("properties"), Entity.Properties)))
			{
				FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(Request, World.Revision, MakeError("INVALID_ARGUMENT", "Spawn arguments are invalid"));
				Impl->StoreCached(Key, Fingerprint, Failure);
				return Failure;
			}
			World.Entities.emplace(Entity.EntityId, Entity);
			++World.Revision;
			FWorldAdapterChange Change;
			Change.Operation = "spawn";
			Change.EntityId = Entity.EntityId;
			Change.Before = nullptr;
			Change.After = FWorldAdapterJsonProtocol::ToJson(Entity);
			ToolResult.Changes.push_back(Change);
			Response.Changes.push_back(std::move(Change));
			ToolResult.Data = { { "entity", FWorldAdapterJsonProtocol::ToJson(Entity) } };
		}
		else
		{
			if (!Call.Args.is_object() || !Call.Args.contains("entity_id") || !Call.Args.at("entity_id").is_string() || !Call.Args.contains("transform"))
			{
				FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(Request, World.Revision, MakeError("INVALID_ARGUMENT", "Transform arguments are invalid"));
				Impl->StoreCached(Key, Fingerprint, Failure);
				return Failure;
			}
			const std::string EntityId = Call.Args.at("entity_id").get<std::string>();
			const auto EntityIt = World.Entities.find(EntityId);
			if (EntityIt == World.Entities.end())
			{
				FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(Request, World.Revision, MakeError("ENTITY_NOT_FOUND", "Entity not found: " + EntityId, { { "entity_id", EntityId } }));
				Impl->StoreCached(Key, Fingerprint, Failure);
				return Failure;
			}
			const FWorldAdapterTransform Before = EntityIt->second.Transform;
			FWorldAdapterTransform After = Before;
			if (!ApplyTransformPatch(Call.Args.at("transform"), After))
			{
				FWorldAdapterExecuteResponse Failure = MakeExecuteFailure(Request, World.Revision, MakeError("INVALID_ARGUMENT", "Transform values are invalid"));
				Impl->StoreCached(Key, Fingerprint, Failure);
				return Failure;
			}
			EntityIt->second.Transform = After;
			++World.Revision;
			FWorldAdapterChange Change;
			Change.Operation = "set_transform";
			Change.EntityId = EntityId;
			Change.Before = TransformJson(Before);
			Change.After = TransformJson(After);
			ToolResult.Changes.push_back(Change);
			Response.Changes.push_back(std::move(Change));
			ToolResult.Data = { { "entity", FWorldAdapterJsonProtocol::ToJson(EntityIt->second) } };
		}

		Response.AfterRevision = World.Revision;
		ToolResult.AfterRevision = World.Revision;
		Response.ToolResults.push_back(std::move(ToolResult));
		Impl->StoreCached(Key, Fingerprint, Response);
		return Response;
	}
	catch (...)
	{
		return MakeExecuteFailure(Request, 0, MakeError("INTERNAL_ERROR", "Stub execution failed safely", {}, true));
	}
}

FWorldAdapterUndoResponse FStubWorldBackend::Undo(const FWorldAdapterUndoRequest& Request) noexcept
{
	FWorldAdapterUndoResponse Response;
	Response.RequestId = Request.RequestId;
	Response.SessionId = Request.SessionId;
	Response.WorldId = Request.WorldId;
	try
	{
		const FStubWorldState* World = Impl ? Impl->FindWorld(Request.SessionId, Request.WorldId) : nullptr;
		Response.BeforeRevision = World ? World->Revision : 0;
		Response.AfterRevision = Response.BeforeRevision;
		Response.Error = MakeError("UNDO_NOT_AVAILABLE", "World adapter does not support undo");
		Response.Data = nullptr;
		return Response;
	}
	catch (...)
	{
		Response.Error = MakeError("INTERNAL_ERROR", "Stub undo failed safely", {}, true);
		Response.Data = nullptr;
		return Response;
	}
}

void FStubWorldBackend::BeginShutdown() noexcept
{
	if (Impl)
	{
		Impl->bShuttingDown = true;
	}
}

void FStubWorldBackend::Shutdown() noexcept
{
	if (!Impl || Impl->bShutdown)
	{
		return;
	}
	Impl->bShuttingDown = true;
	Impl->bShutdown = true;
}

std::uint64_t FStubWorldBackend::GetRevision(const std::string& SessionId, const std::string& WorldId) const noexcept
{
	const FStubWorldState* World = Impl ? Impl->FindWorld(SessionId, WorldId) : nullptr;
	return World ? World->Revision : 0;
}

std::size_t FStubWorldBackend::GetEntityCount(const std::string& SessionId, const std::string& WorldId) const noexcept
{
	const FStubWorldState* World = Impl ? Impl->FindWorld(SessionId, WorldId) : nullptr;
	return World ? World->Entities.size() : 0;
}

std::uint64_t FStubWorldBackend::GetExecutionCount() const noexcept
{
	return Impl ? Impl->ExecutionCount : 0;
}

std::thread::id FStubWorldBackend::GetLastExecutionThreadId() const noexcept
{
	return Impl ? Impl->LastExecutionThreadId : std::thread::id();
}

} // namespace Maho
