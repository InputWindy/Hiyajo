#include <WorldAdapter/Protocol/Json.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Maho
{

namespace
{

constexpr std::size_t MaxDtoDepth = 64;
constexpr std::size_t MaxProtocolArrayLength = 1000;
constexpr double PositionLimit = 100000.0;
constexpr double RotationLimit = 360000.0;
constexpr double ScaleMinimum = 0.0001;
constexpr double ScaleMaximum = 10000.0;

const std::unordered_set<std::string> KnownToolNames =
{
	"world.get_summary",
	"world.query_entities",
	"entity.get",
	"entity.spawn_primitive",
	"entity.destroy",
	"entity.set_transform",
	"entity.set_property",
	"history.undo",
};

const std::unordered_set<std::string> PrimitiveTypes =
{
	"cube",
	"sphere",
	"cylinder",
	"plane",
};

void SetError(
	FWorldAdapterError& OutError,
	std::string Code,
	std::string Message,
	FWorldAdapterJson Details = FWorldAdapterJson::object(),
	bool bRetryable = false)
{
	OutError.Code = std::move(Code);
	OutError.Message = std::move(Message);
	OutError.Details = Details.is_object() ? std::move(Details) : FWorldAdapterJson::object();
	OutError.bRetryable = bRetryable;
}

bool FailField(
	FWorldAdapterError& OutError,
	const std::string& Field,
	const std::string& Message,
	const char* Code = "INVALID_REQUEST")
{
	SetError(OutError, Code, Message, { { "field", Field } });
	return false;
}

bool ValidateJsonValue(
	const FWorldAdapterJson& Value,
	const std::string& Field,
	std::size_t Depth,
	FWorldAdapterError& OutError)
{
	if (Depth > MaxDtoDepth)
	{
		return FailField(OutError, Field, "JSON value exceeds the maximum DTO depth");
	}
	if (Value.is_number_float() && !std::isfinite(Value.get<double>()))
	{
		return FailField(OutError, Field, "JSON value contains a non-finite number");
	}
	if (Value.is_array())
	{
		if (Value.size() > MaxProtocolArrayLength)
		{
			return FailField(OutError, Field, "JSON array exceeds the protocol safety limit");
		}
		for (std::size_t Index = 0; Index < Value.size(); ++Index)
		{
			if (!ValidateJsonValue(
				Value[Index],
				Field + "[" + std::to_string(Index) + "]",
				Depth + 1,
				OutError))
			{
				return false;
			}
		}
	}
	else if (Value.is_object())
	{
		for (auto It = Value.begin(); It != Value.end(); ++It)
		{
			if (It.key() == "__proto__" || It.key() == "prototype" || It.key() == "constructor")
			{
				return FailField(OutError, Field + "." + It.key(), "JSON value contains a forbidden key");
			}
			if (!ValidateJsonValue(It.value(), Field + "." + It.key(), Depth + 1, OutError))
			{
				return false;
			}
		}
	}
	return true;
}

bool RequireObject(const FWorldAdapterJson& Value, const std::string& Field, FWorldAdapterError& OutError)
{
	return Value.is_object() || FailField(OutError, Field, Field + " must be an object");
}

bool RequireKnownFields(
	const FWorldAdapterJson& Value,
	std::initializer_list<const char*> Fields,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	if (!RequireObject(Value, Field, OutError))
	{
		return false;
	}
	std::unordered_set<std::string> Known;
	Known.reserve(Fields.size());
	for (const char* Name : Fields)
	{
		Known.emplace(Name);
	}
	for (auto It = Value.begin(); It != Value.end(); ++It)
	{
		if (!Known.contains(It.key()))
		{
			return FailField(
				OutError,
				Field + "." + It.key(),
				Field + " contains an unknown field");
		}
	}
	return true;
}

bool RequireFields(
	const FWorldAdapterJson& Value,
	std::initializer_list<const char*> Fields,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	for (const char* Name : Fields)
	{
		if (!Value.contains(Name))
		{
			return FailField(
				OutError,
				Field + "." + Name,
				Field + " is missing a required field");
		}
	}
	return true;
}

bool ReadString(
	const FWorldAdapterJson& Object,
	const char* Name,
	std::size_t Minimum,
	std::size_t Maximum,
	std::string& OutValue,
	const std::string& Prefix,
	FWorldAdapterError& OutError)
{
	const std::string Field = Prefix.empty() ? Name : Prefix + "." + Name;
	if (!Object.contains(Name) || !Object.at(Name).is_string())
	{
		return FailField(OutError, Field, Field + " must be a string");
	}
	OutValue = Object.at(Name).get<std::string>();
	if (OutValue.size() < Minimum || OutValue.size() > Maximum)
	{
		return FailField(OutError, Field, Field + " has an invalid length");
	}
	return true;
}

bool ReadUuid(
	const FWorldAdapterJson& Object,
	const char* Name,
	std::string& OutValue,
	const std::string& Prefix,
	FWorldAdapterError& OutError)
{
	const std::string Field = Prefix.empty() ? Name : Prefix + "." + Name;
	if (!ReadString(Object, Name, 36, 36, OutValue, Prefix, OutError) ||
		!FWorldAdapterJsonProtocol::IsUuid(OutValue))
	{
		return FailField(OutError, Field, Field + " must be a UUID");
	}
	return true;
}

bool ReadBool(
	const FWorldAdapterJson& Object,
	const char* Name,
	bool& OutValue,
	const std::string& Prefix,
	FWorldAdapterError& OutError)
{
	const std::string Field = Prefix.empty() ? Name : Prefix + "." + Name;
	if (!Object.contains(Name) || !Object.at(Name).is_boolean())
	{
		return FailField(OutError, Field, Field + " must be a boolean");
	}
	OutValue = Object.at(Name).get<bool>();
	return true;
}

bool ReadSafeIntegerValue(
	const FWorldAdapterJson& Value,
	std::uint64_t& OutValue,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	if (Value.is_number_unsigned())
	{
		OutValue = Value.get<std::uint64_t>();
	}
	else if (Value.is_number_integer())
	{
		const std::int64_t Signed = Value.get<std::int64_t>();
		if (Signed < 0)
		{
			return FailField(OutError, Field, Field + " must be a non-negative safe integer");
		}
		OutValue = static_cast<std::uint64_t>(Signed);
	}
	else if (Value.is_number_float())
	{
		const double Number = Value.get<double>();
		if (!std::isfinite(Number) || Number < 0.0 || std::floor(Number) != Number)
		{
			return FailField(OutError, Field, Field + " must be a non-negative safe integer");
		}
		OutValue = static_cast<std::uint64_t>(Number);
	}
	else
	{
		return FailField(OutError, Field, Field + " must be a non-negative safe integer");
	}
	if (OutValue > WorldAdapterMaxSafeInteger)
	{
		return FailField(OutError, Field, Field + " must be a non-negative safe integer");
	}
	return true;
}

bool ReadSafeInteger(
	const FWorldAdapterJson& Object,
	const char* Name,
	std::uint64_t& OutValue,
	const std::string& Prefix,
	FWorldAdapterError& OutError)
{
	const std::string Field = Prefix.empty() ? Name : Prefix + "." + Name;
	if (!Object.contains(Name))
	{
		return FailField(OutError, Field, Field + " is required");
	}
	return ReadSafeIntegerValue(Object.at(Name), OutValue, Field, OutError);
}

bool ValidateProtocolVersion(const FWorldAdapterJson& Value, FWorldAdapterError& OutError)
{
	std::string Version;
	if (!ReadString(Value, "adapter_protocol_version", 1, 32, Version, "", OutError))
	{
		return false;
	}
	if (Version != WorldAdapterProtocolVersion)
	{
		SetError(
			OutError,
			"PROTOCOL_VERSION_INCOMPATIBLE",
			"World adapter protocol version is incompatible",
			{
				{ "expected", WorldAdapterProtocolVersion },
				{ "received", Version },
			});
		return false;
	}
	return true;
}

bool ReadVector3(
	const FWorldAdapterJson& Value,
	std::array<double, 3>& OutValue,
	double Minimum,
	double Maximum,
	bool bExclusiveMinimum,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	if (!Value.is_array() || Value.size() != 3)
	{
		return FailField(OutError, Field, Field + " must contain exactly three numbers");
	}
	for (std::size_t Index = 0; Index < 3; ++Index)
	{
		if (!Value[Index].is_number())
		{
			return FailField(OutError, Field + "[" + std::to_string(Index) + "]", Field + " must contain numbers");
		}
		const double Number = Value[Index].get<double>();
		if (!std::isfinite(Number) ||
			(bExclusiveMinimum ? Number <= Minimum : Number < Minimum) ||
			Number > Maximum)
		{
			return FailField(OutError, Field + "[" + std::to_string(Index) + "]", Field + " contains an out-of-range number");
		}
		OutValue[Index] = Number;
	}
	return true;
}

bool ParseTransformPatch(
	const FWorldAdapterJson& Value,
	FWorldAdapterTransformPatch& OutTransform,
	const std::string& Field,
	bool bRequireOne,
	FWorldAdapterError& OutError)
{
	if (!RequireKnownFields(Value, { "position", "rotation", "scale" }, Field, OutError))
	{
		return false;
	}
	if (bRequireOne && Value.empty())
	{
		return FailField(OutError, Field, Field + " must contain at least one transform component");
	}
	if (Value.contains("position"))
	{
		std::array<double, 3> Position{};
		if (!ReadVector3(Value.at("position"), Position, -PositionLimit, PositionLimit, false, Field + ".position", OutError))
		{
			return false;
		}
		OutTransform.Position = Position;
	}
	if (Value.contains("rotation"))
	{
		std::array<double, 3> Rotation{};
		if (!ReadVector3(Value.at("rotation"), Rotation, -RotationLimit, RotationLimit, false, Field + ".rotation", OutError))
		{
			return false;
		}
		OutTransform.Rotation = Rotation;
	}
	if (Value.contains("scale"))
	{
		std::array<double, 3> Scale{};
		if (!ReadVector3(Value.at("scale"), Scale, ScaleMinimum, ScaleMaximum, true, Field + ".scale", OutError))
		{
			return false;
		}
		OutTransform.Scale = Scale;
	}
	return true;
}

bool ParseFullTransform(
	const FWorldAdapterJson& Value,
	FWorldAdapterTransform& OutTransform,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	if (!RequireKnownFields(Value, { "position", "rotation", "scale" }, Field, OutError) ||
		!RequireFields(Value, { "position", "rotation", "scale" }, Field, OutError))
	{
		return false;
	}
	return ReadVector3(Value.at("position"), OutTransform.Position, -PositionLimit, PositionLimit, false, Field + ".position", OutError) &&
		ReadVector3(Value.at("rotation"), OutTransform.Rotation, -RotationLimit, RotationLimit, false, Field + ".rotation", OutError) &&
		ReadVector3(Value.at("scale"), OutTransform.Scale, ScaleMinimum, ScaleMaximum, true, Field + ".scale", OutError);
}

bool ParseProperties(
	const FWorldAdapterJson& Value,
	FWorldAdapterEntityProperties& OutProperties,
	const std::string& Field,
	bool bRequireAll,
	FWorldAdapterError& OutError)
{
	if (!RequireKnownFields(Value, { "color", "visible", "label" }, Field, OutError))
	{
		return false;
	}
	if (bRequireAll && !RequireFields(Value, { "color", "visible", "label" }, Field, OutError))
	{
		return false;
	}
	if (Value.contains("color"))
	{
		const FWorldAdapterJson& Color = Value.at("color");
		if (!Color.is_array() || Color.size() != 4)
		{
			return FailField(OutError, Field + ".color", "color must contain exactly four numbers");
		}
		for (std::size_t Index = 0; Index < 4; ++Index)
		{
			if (!Color[Index].is_number())
			{
				return FailField(OutError, Field + ".color", "color must contain numbers");
			}
			const double Number = Color[Index].get<double>();
			if (!std::isfinite(Number) || Number < 0.0 || Number > 1.0)
			{
				return FailField(OutError, Field + ".color", "color contains an out-of-range number");
			}
			OutProperties.Color[Index] = Number;
		}
	}
	if (Value.contains("visible"))
	{
		if (!Value.at("visible").is_boolean())
		{
			return FailField(OutError, Field + ".visible", "visible must be a boolean");
		}
		OutProperties.bVisible = Value.at("visible").get<bool>();
	}
	if (Value.contains("label"))
	{
		if (!Value.at("label").is_string())
		{
			return FailField(OutError, Field + ".label", "label must be a string");
		}
		OutProperties.Label = Value.at("label").get<std::string>();
		if (OutProperties.Label.size() > 256)
		{
			return FailField(OutError, Field + ".label", "label exceeds the maximum length");
		}
	}
	return true;
}

bool ParseEntity(
	const FWorldAdapterJson& Value,
	FWorldAdapterEntity& OutEntity,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	if (!RequireKnownFields(
		Value,
		{ "entity_id", "generation", "name", "entity_type", "primitive_type", "transform", "properties" },
		Field,
		OutError) ||
		!RequireFields(
			Value,
			{ "entity_id", "generation", "name", "entity_type", "primitive_type", "transform", "properties" },
			Field,
			OutError))
	{
		return false;
	}
	if (!ReadString(Value, "entity_id", 1, 128, OutEntity.EntityId, Field, OutError) ||
		!ReadSafeInteger(Value, "generation", OutEntity.Generation, Field, OutError) ||
		!ReadString(Value, "name", 1, 128, OutEntity.Name, Field, OutError) ||
		!ReadString(Value, "entity_type", 1, 128, OutEntity.EntityType, Field, OutError) ||
		!ReadString(Value, "primitive_type", 1, 128, OutEntity.PrimitiveType, Field, OutError))
	{
		return false;
	}
	if (OutEntity.EntityType != "primitive" || !PrimitiveTypes.contains(OutEntity.PrimitiveType))
	{
		return FailField(OutError, Field + ".primitive_type", "Entity type is not a supported primitive");
	}
	return ParseFullTransform(Value.at("transform"), OutEntity.Transform, Field + ".transform", OutError) &&
		ParseProperties(Value.at("properties"), OutEntity.Properties, Field + ".properties", true, OutError);
}

bool ParseCapabilities(
	const FWorldAdapterJson& Value,
	FWorldAdapterCapabilities& OutCapabilities,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	if (!RequireKnownFields(
		Value,
		{
			"supports_atomic_transactions",
			"supports_dry_run",
			"supports_undo",
			"supports_idempotency",
			"max_tool_calls",
			"supported_tools",
		},
		Field,
		OutError) ||
		!RequireFields(
			Value,
			{
				"supports_atomic_transactions",
				"supports_dry_run",
				"supports_undo",
				"supports_idempotency",
				"max_tool_calls",
				"supported_tools",
			},
			Field,
			OutError))
	{
		return false;
	}
	if (!ReadBool(Value, "supports_atomic_transactions", OutCapabilities.bSupportsAtomicTransactions, Field, OutError) ||
		!ReadBool(Value, "supports_dry_run", OutCapabilities.bSupportsDryRun, Field, OutError) ||
		!ReadBool(Value, "supports_undo", OutCapabilities.bSupportsUndo, Field, OutError) ||
		!ReadBool(Value, "supports_idempotency", OutCapabilities.bSupportsIdempotency, Field, OutError))
	{
		return false;
	}
	std::uint64_t MaxToolCalls = 0;
	if (!ReadSafeInteger(Value, "max_tool_calls", MaxToolCalls, Field, OutError) || MaxToolCalls < 1 || MaxToolCalls > 1000)
	{
		return FailField(OutError, Field + ".max_tool_calls", "max_tool_calls must be from 1 through 1000");
	}
	OutCapabilities.MaxToolCalls = static_cast<std::uint32_t>(MaxToolCalls);
	if (!Value.at("supported_tools").is_array() || Value.at("supported_tools").empty())
	{
		return FailField(OutError, Field + ".supported_tools", "supported_tools must be a non-empty array");
	}
	std::unordered_set<std::string> Unique;
	OutCapabilities.SupportedTools.clear();
	for (std::size_t Index = 0; Index < Value.at("supported_tools").size(); ++Index)
	{
		const FWorldAdapterJson& Tool = Value.at("supported_tools")[Index];
		if (!Tool.is_string())
		{
			return FailField(OutError, Field + ".supported_tools[" + std::to_string(Index) + "]", "supported_tools entries must be strings");
		}
		const std::string Name = Tool.get<std::string>();
		if (Name.empty() || Name.size() > 128 || !KnownToolNames.contains(Name))
		{
			return FailField(OutError, Field + ".supported_tools[" + std::to_string(Index) + "]", "supported_tools contains an unknown tool", "CAPABILITY_INSUFFICIENT");
		}
		if (!Unique.emplace(Name).second)
		{
			return FailField(OutError, Field + ".supported_tools", "supported_tools must not contain duplicates", "CAPABILITY_INSUFFICIENT");
		}
		OutCapabilities.SupportedTools.push_back(Name);
	}
	const bool bAdvertisesUndo = Unique.contains("history.undo");
	if (OutCapabilities.bSupportsUndo != bAdvertisesUndo)
	{
		return FailField(OutError, Field + ".supports_undo", "Undo capability and history.undo dependency are inconsistent", "CAPABILITY_INSUFFICIENT");
	}
	if (!OutCapabilities.bSupportsAtomicTransactions && OutCapabilities.MaxToolCalls != 1)
	{
		return FailField(OutError, Field + ".max_tool_calls", "A non-atomic adapter must declare max_tool_calls=1", "CAPABILITY_INSUFFICIENT");
	}
	return true;
}

bool ParseError(
	const FWorldAdapterJson& Value,
	std::optional<FWorldAdapterError>& OutValue,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	if (Value.is_null())
	{
		OutValue.reset();
		return true;
	}
	if (!RequireKnownFields(Value, { "code", "message", "details", "retryable" }, Field, OutError) ||
		!RequireFields(Value, { "code", "message", "details", "retryable" }, Field, OutError))
	{
		return false;
	}
	FWorldAdapterError Error;
	if (!ReadString(Value, "code", 1, 128, Error.Code, Field, OutError) ||
		!ReadString(Value, "message", 1, 2048, Error.Message, Field, OutError) ||
		!ReadBool(Value, "retryable", Error.bRetryable, Field, OutError))
	{
		return false;
	}
	if (!Value.at("details").is_object())
	{
		return FailField(OutError, Field + ".details", "Error details must be an object");
	}
	Error.Details = Value.at("details");
	OutValue = std::move(Error);
	return true;
}

bool ParseChange(
	const FWorldAdapterJson& Value,
	FWorldAdapterChange& OutChange,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	if (!RequireKnownFields(Value, { "operation", "entity_id", "property_name", "before", "after" }, Field, OutError) ||
		!RequireFields(Value, { "operation", "before", "after" }, Field, OutError) ||
		!ReadString(Value, "operation", 1, 128, OutChange.Operation, Field, OutError))
	{
		return false;
	}
	if (Value.contains("entity_id"))
	{
		std::string EntityId;
		if (!ReadString(Value, "entity_id", 1, 128, EntityId, Field, OutError))
		{
			return false;
		}
		OutChange.EntityId = std::move(EntityId);
	}
	if (Value.contains("property_name"))
	{
		std::string PropertyName;
		if (!ReadString(Value, "property_name", 1, 128, PropertyName, Field, OutError))
		{
			return false;
		}
		OutChange.PropertyName = std::move(PropertyName);
	}
	OutChange.Before = Value.at("before");
	OutChange.After = Value.at("after");
	return true;
}

bool ParseChanges(
	const FWorldAdapterJson& Value,
	std::vector<FWorldAdapterChange>& OutChanges,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	if (!Value.is_array() || Value.size() > MaxProtocolArrayLength)
	{
		return FailField(OutError, Field, Field + " must be a bounded array");
	}
	OutChanges.clear();
	OutChanges.reserve(Value.size());
	for (std::size_t Index = 0; Index < Value.size(); ++Index)
	{
		FWorldAdapterChange Change;
		if (!ParseChange(Value[Index], Change, Field + "[" + std::to_string(Index) + "]", OutError))
		{
			return false;
		}
		OutChanges.push_back(std::move(Change));
	}
	return true;
}

bool ParseToolResult(
	const FWorldAdapterJson& Value,
	FWorldAdapterToolResult& OutResult,
	const std::string& Field,
	FWorldAdapterError& OutError)
{
	if (!RequireKnownFields(
		Value,
		{
			"ok", "request_id", "tool_call_id", "before_revision", "after_revision", "changes",
			"undo_token", "error", "data", "dry_run", "rolled_back",
		},
		Field,
		OutError) ||
		!RequireFields(
			Value,
			{ "ok", "request_id", "tool_call_id", "before_revision", "after_revision", "changes", "undo_token", "error" },
			Field,
			OutError) ||
		!ReadBool(Value, "ok", OutResult.bOk, Field, OutError) ||
		!ReadUuid(Value, "request_id", OutResult.RequestId, Field, OutError) ||
		!ReadSafeInteger(Value, "before_revision", OutResult.BeforeRevision, Field, OutError) ||
		!ReadSafeInteger(Value, "after_revision", OutResult.AfterRevision, Field, OutError))
	{
		return false;
	}
	if (Value.at("tool_call_id").is_null())
	{
		OutResult.ToolCallId.reset();
	}
	else
	{
		std::string ToolCallId;
		if (!ReadUuid(Value, "tool_call_id", ToolCallId, Field, OutError))
		{
			return false;
		}
		OutResult.ToolCallId = std::move(ToolCallId);
	}
	if (!ParseChanges(Value.at("changes"), OutResult.Changes, Field + ".changes", OutError) ||
		!ParseError(Value.at("error"), OutResult.Error, Field + ".error", OutError))
	{
		return false;
	}
	if (Value.at("undo_token").is_null())
	{
		OutResult.UndoToken.reset();
	}
	else
	{
		std::string UndoToken;
		if (!ReadUuid(Value, "undo_token", UndoToken, Field, OutError))
		{
			return false;
		}
		OutResult.UndoToken = std::move(UndoToken);
	}
	OutResult.Data = Value.value("data", FWorldAdapterJson());
	if (Value.contains("dry_run"))
	{
		if (!Value.at("dry_run").is_boolean())
		{
			return FailField(OutError, Field + ".dry_run", "dry_run must be a boolean");
		}
		OutResult.bDryRun = Value.at("dry_run").get<bool>();
	}
	if (Value.contains("rolled_back"))
	{
		if (!Value.at("rolled_back").is_boolean())
		{
			return FailField(OutError, Field + ".rolled_back", "rolled_back must be a boolean");
		}
		OutResult.bRolledBack = Value.at("rolled_back").get<bool>();
	}
	return true;
}

bool ParseBaseIdentity(
	const FWorldAdapterJson& Value,
	std::string& OutRequestId,
	std::string& OutSessionId,
	std::string& OutWorldId,
	FWorldAdapterError& OutError)
{
	return ReadUuid(Value, "request_id", OutRequestId, "", OutError) &&
		ReadUuid(Value, "session_id", OutSessionId, "", OutError) &&
		ReadString(Value, "world_id", 1, 128, OutWorldId, "", OutError);
}

bool ValidateCorrelation(
	const std::string& RequestId,
	const std::string& SessionId,
	const std::string& WorldId,
	const std::string& ExpectedRequestId,
	const std::string& ExpectedSessionId,
	const std::string& ExpectedWorldId,
	FWorldAdapterError& OutError)
{
	if (RequestId != ExpectedRequestId || SessionId != ExpectedSessionId || WorldId != ExpectedWorldId)
	{
		SetError(OutError, "CORRELATION_MISMATCH", "World adapter response identity does not match the request");
		return false;
	}
	return true;
}

FWorldAdapterJson TransformToJson(const FWorldAdapterTransform& Value)
{
	return {
		{ "position", Value.Position },
		{ "rotation", Value.Rotation },
		{ "scale", Value.Scale },
	};
}

FWorldAdapterJson PropertiesToJson(const FWorldAdapterEntityProperties& Value)
{
	return {
		{ "color", Value.Color },
		{ "visible", Value.bVisible },
		{ "label", Value.Label },
	};
}

FWorldAdapterJson ChangeToJson(const FWorldAdapterChange& Value)
{
	FWorldAdapterJson Result = {
		{ "operation", Value.Operation },
		{ "before", Value.Before },
		{ "after", Value.After },
	};
	if (Value.EntityId)
	{
		Result["entity_id"] = *Value.EntityId;
	}
	if (Value.PropertyName)
	{
		Result["property_name"] = *Value.PropertyName;
	}
	return Result;
}

FWorldAdapterJson ToolResultToJson(const FWorldAdapterToolResult& Value)
{
	FWorldAdapterJson Changes = FWorldAdapterJson::array();
	for (const FWorldAdapterChange& Change : Value.Changes)
	{
		Changes.push_back(ChangeToJson(Change));
	}
	FWorldAdapterJson Result = {
		{ "ok", Value.bOk },
		{ "request_id", Value.RequestId },
		{ "tool_call_id", Value.ToolCallId ? FWorldAdapterJson(*Value.ToolCallId) : FWorldAdapterJson(nullptr) },
		{ "before_revision", Value.BeforeRevision },
		{ "after_revision", Value.AfterRevision },
		{ "changes", std::move(Changes) },
		{ "undo_token", Value.UndoToken ? FWorldAdapterJson(*Value.UndoToken) : FWorldAdapterJson(nullptr) },
		{ "error", Value.Error ? FWorldAdapterJsonProtocol::ToJson(*Value.Error) : FWorldAdapterJson(nullptr) },
		{ "data", Value.Data },
		{ "dry_run", Value.bDryRun },
	};
	if (Value.bRolledBack)
	{
		Result["rolled_back"] = true;
	}
	return Result;
}

} // namespace

FWorldAdapterCapabilities FWorldAdapterJsonProtocol::MinimalCapabilities()
{
	FWorldAdapterCapabilities Result;
	Result.SupportedTools =
	{
		"world.get_summary",
		"entity.spawn_primitive",
		"entity.set_transform",
	};
	return Result;
}

bool FWorldAdapterJsonProtocol::ParseJson(
	std::string_view Text,
	FWorldAdapterJson& OutValue,
	FWorldAdapterError& OutError,
	std::size_t SizeLimit) noexcept
{
	try
	{
		if (Text.size() > SizeLimit)
		{
			SetError(OutError, "REQUEST_TOO_LARGE", "JSON body exceeds the configured size limit");
			return false;
		}
		OutValue = FWorldAdapterJson::parse(Text.begin(), Text.end(), nullptr, false, false);
		if (OutValue.is_discarded())
		{
			SetError(OutError, "INVALID_JSON", "Request body is not valid JSON");
			return false;
		}
		return ValidateJsonValue(OutValue, "body", 0, OutError);
	}
	catch (...)
	{
		SetError(OutError, "INVALID_JSON", "Request body could not be parsed safely");
		return false;
	}
}

bool FWorldAdapterJsonProtocol::ParseSnapshotRequest(
	std::string_view Text,
	FWorldAdapterSnapshotRequest& OutRequest,
	FWorldAdapterError& OutError) noexcept
{
	try
	{
		FWorldAdapterJson Value;
		if (!ParseJson(Text, Value, OutError) ||
			!RequireKnownFields(Value, { "adapter_protocol_version", "request_id", "session_id", "world_id" }, "snapshot request", OutError) ||
			!RequireFields(Value, { "adapter_protocol_version", "request_id", "session_id", "world_id" }, "snapshot request", OutError) ||
			!ValidateProtocolVersion(Value, OutError))
		{
			return false;
		}
		return ParseBaseIdentity(Value, OutRequest.RequestId, OutRequest.SessionId, OutRequest.WorldId, OutError);
	}
	catch (...)
	{
		SetError(OutError, "INVALID_REQUEST", "Snapshot request validation failed safely");
		return false;
	}
}

bool FWorldAdapterJsonProtocol::ParseExecuteRequest(
	std::string_view Text,
	FWorldAdapterExecuteRequest& OutRequest,
	FWorldAdapterError& OutError) noexcept
{
	try
	{
		FWorldAdapterJson Value;
		if (!ParseJson(Text, Value, OutError) ||
			!RequireKnownFields(
				Value,
				{
					"adapter_protocol_version", "request_id", "session_id", "world_id",
					"expected_revision", "dry_run", "atomic", "tool_calls",
				},
				"execute request",
				OutError) ||
			!RequireFields(
				Value,
				{
					"adapter_protocol_version", "request_id", "session_id", "world_id",
					"expected_revision", "dry_run", "atomic", "tool_calls",
				},
				"execute request",
				OutError) ||
			!ValidateProtocolVersion(Value, OutError) ||
			!ParseBaseIdentity(Value, OutRequest.RequestId, OutRequest.SessionId, OutRequest.WorldId, OutError) ||
			!ReadSafeInteger(Value, "expected_revision", OutRequest.ExpectedRevision, "", OutError) ||
			!ReadBool(Value, "dry_run", OutRequest.bDryRun, "", OutError) ||
			!ReadBool(Value, "atomic", OutRequest.bAtomic, "", OutError))
		{
			return false;
		}
		const FWorldAdapterJson& ToolCalls = Value.at("tool_calls");
		if (!ToolCalls.is_array() || ToolCalls.empty() || ToolCalls.size() > MaxProtocolArrayLength)
		{
			return FailField(OutError, "tool_calls", "tool_calls must contain from 1 through 1000 entries");
		}
		OutRequest.ToolCalls.clear();
		OutRequest.ToolCalls.reserve(ToolCalls.size());
		for (std::size_t Index = 0; Index < ToolCalls.size(); ++Index)
		{
			const std::string Prefix = "tool_calls[" + std::to_string(Index) + "]";
			const FWorldAdapterJson& Tool = ToolCalls[Index];
			if (!RequireKnownFields(Tool, { "tool_call_id", "tool_name", "args" }, Prefix, OutError) ||
				!RequireFields(Tool, { "tool_call_id", "tool_name", "args" }, Prefix, OutError))
			{
				return false;
			}
			FWorldAdapterToolCall Call;
			if (!ReadUuid(Tool, "tool_call_id", Call.ToolCallId, Prefix, OutError) ||
				!ReadString(Tool, "tool_name", 1, 128, Call.ToolName, Prefix, OutError))
			{
				return false;
			}
			if (!KnownToolNames.contains(Call.ToolName))
			{
				return FailField(OutError, Prefix + ".tool_name", "ToolCall contains an unknown tool", "UNKNOWN_TOOL");
			}
			if (!Tool.at("args").is_object())
			{
				return FailField(OutError, Prefix + ".args", "ToolCall args must be an object");
			}
			Call.Args = Tool.at("args");
			if (Call.ToolName == "world.get_summary")
			{
				if (!Call.Args.empty())
				{
					return FailField(OutError, Prefix + ".args", "world.get_summary args must be empty");
				}
			}
			else if (Call.ToolName == "entity.spawn_primitive")
			{
				if (!RequireKnownFields(Call.Args, { "primitive_type", "name", "transform", "properties" }, Prefix + ".args", OutError) ||
					!RequireFields(Call.Args, { "primitive_type" }, Prefix + ".args", OutError))
				{
					return false;
				}
				std::string PrimitiveType;
				if (!ReadString(Call.Args, "primitive_type", 1, 128, PrimitiveType, Prefix + ".args", OutError) ||
					!PrimitiveTypes.contains(PrimitiveType))
				{
					return FailField(OutError, Prefix + ".args.primitive_type", "primitive_type is not supported");
				}
				if (Call.Args.contains("name"))
				{
					std::string Name;
					if (!ReadString(Call.Args, "name", 1, 128, Name, Prefix + ".args", OutError))
					{
						return false;
					}
				}
				if (Call.Args.contains("transform"))
				{
					FWorldAdapterTransformPatch Transform;
					if (!ParseTransformPatch(Call.Args.at("transform"), Transform, Prefix + ".args.transform", false, OutError))
					{
						return false;
					}
				}
				if (Call.Args.contains("properties"))
				{
					FWorldAdapterEntityProperties Properties;
					if (!ParseProperties(Call.Args.at("properties"), Properties, Prefix + ".args.properties", false, OutError))
					{
						return false;
					}
				}
			}
			else if (Call.ToolName == "entity.set_transform")
			{
				if (!RequireKnownFields(Call.Args, { "entity_id", "transform" }, Prefix + ".args", OutError) ||
					!RequireFields(Call.Args, { "entity_id", "transform" }, Prefix + ".args", OutError))
				{
					return false;
				}
				std::string EntityId;
				FWorldAdapterTransformPatch Transform;
				if (!ReadUuid(Call.Args, "entity_id", EntityId, Prefix + ".args", OutError) ||
					!ParseTransformPatch(Call.Args.at("transform"), Transform, Prefix + ".args.transform", true, OutError))
				{
					return false;
				}
			}
			OutRequest.ToolCalls.push_back(std::move(Call));
		}
		OutRequest.CanonicalPayload = std::move(Value);
		return true;
	}
	catch (...)
	{
		SetError(OutError, "INVALID_REQUEST", "Execute request validation failed safely");
		return false;
	}
}

bool FWorldAdapterJsonProtocol::ParseUndoRequest(
	std::string_view Text,
	FWorldAdapterUndoRequest& OutRequest,
	FWorldAdapterError& OutError) noexcept
{
	try
	{
		FWorldAdapterJson Value;
		if (!ParseJson(Text, Value, OutError) ||
			!RequireKnownFields(
				Value,
				{ "adapter_protocol_version", "request_id", "session_id", "world_id", "expected_revision", "undo_token" },
				"undo request",
				OutError) ||
			!RequireFields(
				Value,
				{ "adapter_protocol_version", "request_id", "session_id", "world_id", "expected_revision", "undo_token" },
				"undo request",
				OutError) ||
			!ValidateProtocolVersion(Value, OutError) ||
			!ParseBaseIdentity(Value, OutRequest.RequestId, OutRequest.SessionId, OutRequest.WorldId, OutError) ||
			!ReadSafeInteger(Value, "expected_revision", OutRequest.ExpectedRevision, "", OutError))
		{
			return false;
		}
		if (Value.at("undo_token").is_null())
		{
			OutRequest.UndoToken.reset();
		}
		else
		{
			std::string UndoToken;
			if (!ReadUuid(Value, "undo_token", UndoToken, "", OutError))
			{
				return false;
			}
			OutRequest.UndoToken = std::move(UndoToken);
		}
		OutRequest.CanonicalPayload = std::move(Value);
		return true;
	}
	catch (...)
	{
		SetError(OutError, "INVALID_REQUEST", "Undo request validation failed safely");
		return false;
	}
}

bool FWorldAdapterJsonProtocol::ValidateHealthResponse(
	const FWorldAdapterJson& Value,
	FWorldAdapterHealthResponse* OutResponse,
	FWorldAdapterError& OutError) noexcept
{
	try
	{
		if (!ValidateJsonValue(Value, "health response", 0, OutError) ||
			!RequireKnownFields(Value, { "ok", "adapter_protocol_version", "server_name", "server_version", "capabilities", "error" }, "health response", OutError) ||
			!RequireFields(Value, { "ok", "adapter_protocol_version", "server_name", "server_version", "capabilities" }, "health response", OutError) ||
			!ValidateProtocolVersion(Value, OutError))
		{
			return false;
		}
		FWorldAdapterHealthResponse Result;
		if (!ReadBool(Value, "ok", Result.bOk, "", OutError) ||
			!ReadString(Value, "server_name", 1, 128, Result.ServerName, "", OutError) ||
			!ReadString(Value, "server_version", 1, 128, Result.ServerVersion, "", OutError) ||
			!ParseCapabilities(Value.at("capabilities"), Result.Capabilities, "capabilities", OutError))
		{
			return false;
		}
		if (Value.contains("error") && !ParseError(Value.at("error"), Result.Error, "error", OutError))
		{
			return false;
		}
		if (OutResponse)
		{
			*OutResponse = std::move(Result);
		}
		return true;
	}
	catch (...)
	{
		SetError(OutError, "RESPONSE_INVALID", "Health response validation failed safely");
		return false;
	}
}

bool FWorldAdapterJsonProtocol::ValidateSnapshotResponse(
	const FWorldAdapterJson& Value,
	const FWorldAdapterSnapshotRequest* Request,
	FWorldAdapterSnapshotResponse* OutResponse,
	FWorldAdapterError& OutError) noexcept
{
	try
	{
		if (!ValidateJsonValue(Value, "snapshot response", 0, OutError) ||
			!RequireKnownFields(
				Value,
				{
					"ok", "adapter_protocol_version", "request_id", "session_id", "world_id", "world_revision",
					"timestamp_ms", "capabilities", "entities", "history", "error",
				},
				"snapshot response",
				OutError) ||
			!RequireFields(
				Value,
				{
					"ok", "adapter_protocol_version", "request_id", "session_id", "world_id", "world_revision",
					"timestamp_ms", "capabilities", "entities", "error",
				},
				"snapshot response",
				OutError) ||
			!ValidateProtocolVersion(Value, OutError))
		{
			return false;
		}
		FWorldAdapterSnapshotResponse Result;
		if (!ReadBool(Value, "ok", Result.bOk, "", OutError) ||
			!ParseBaseIdentity(Value, Result.RequestId, Result.SessionId, Result.WorldId, OutError) ||
			!ReadSafeInteger(Value, "world_revision", Result.WorldRevision, "", OutError) ||
			!ReadSafeInteger(Value, "timestamp_ms", Result.TimestampMs, "", OutError) ||
			!ParseCapabilities(Value.at("capabilities"), Result.Capabilities, "capabilities", OutError))
		{
			return false;
		}
		if (!Value.at("entities").is_array() || Value.at("entities").size() > MaxProtocolArrayLength)
		{
			return FailField(OutError, "entities", "entities must be a bounded array");
		}
		for (std::size_t Index = 0; Index < Value.at("entities").size(); ++Index)
		{
			FWorldAdapterEntity Entity;
			if (!ParseEntity(Value.at("entities")[Index], Entity, "entities[" + std::to_string(Index) + "]", OutError))
			{
				return false;
			}
			Result.Entities.push_back(std::move(Entity));
		}
		if (Value.contains("history"))
		{
			if (!Value.at("history").is_array() || Value.at("history").size() > MaxProtocolArrayLength)
			{
				return FailField(OutError, "history", "history must be a bounded array");
			}
			for (const FWorldAdapterJson& Entry : Value.at("history"))
			{
				Result.History.push_back(Entry);
			}
		}
		if (!ParseError(Value.at("error"), Result.Error, "error", OutError))
		{
			return false;
		}
		if (Request && !ValidateCorrelation(
			Result.RequestId,
			Result.SessionId,
			Result.WorldId,
			Request->RequestId,
			Request->SessionId,
			Request->WorldId,
			OutError))
		{
			return false;
		}
		if (OutResponse)
		{
			*OutResponse = std::move(Result);
		}
		return true;
	}
	catch (...)
	{
		SetError(OutError, "RESPONSE_INVALID", "Snapshot response validation failed safely");
		return false;
	}
}

bool FWorldAdapterJsonProtocol::ValidateExecuteResponse(
	const FWorldAdapterJson& Value,
	const FWorldAdapterExecuteRequest* Request,
	FWorldAdapterExecuteResponse* OutResponse,
	FWorldAdapterError& OutError) noexcept
{
	try
	{
		if (!ValidateJsonValue(Value, "execute response", 0, OutError) ||
			!RequireKnownFields(
				Value,
				{
					"ok", "adapter_protocol_version", "request_id", "session_id", "world_id", "before_revision",
					"after_revision", "replayed", "tool_results", "changes", "undo_token", "error",
					"failed_tool_call_index", "adapter_metadata",
				},
				"execute response",
				OutError) ||
			!RequireFields(
				Value,
				{
					"ok", "adapter_protocol_version", "request_id", "session_id", "world_id", "before_revision",
					"after_revision", "replayed", "tool_results", "changes", "undo_token", "error",
				},
				"execute response",
				OutError) ||
			!ValidateProtocolVersion(Value, OutError))
		{
			return false;
		}
		FWorldAdapterExecuteResponse Result;
		if (!ReadBool(Value, "ok", Result.bOk, "", OutError) ||
			!ParseBaseIdentity(Value, Result.RequestId, Result.SessionId, Result.WorldId, OutError) ||
			!ReadSafeInteger(Value, "before_revision", Result.BeforeRevision, "", OutError) ||
			!ReadSafeInteger(Value, "after_revision", Result.AfterRevision, "", OutError) ||
			!ReadBool(Value, "replayed", Result.bReplayed, "", OutError) ||
			!ParseChanges(Value.at("changes"), Result.Changes, "changes", OutError) ||
			!ParseError(Value.at("error"), Result.Error, "error", OutError))
		{
			return false;
		}
		if (Result.AfterRevision < Result.BeforeRevision)
		{
			return FailField(OutError, "after_revision", "World adapter response revision moved backward");
		}
		if (!Value.at("tool_results").is_array() || Value.at("tool_results").size() > MaxProtocolArrayLength)
		{
			return FailField(OutError, "tool_results", "tool_results must be a bounded array");
		}
		for (std::size_t Index = 0; Index < Value.at("tool_results").size(); ++Index)
		{
			FWorldAdapterToolResult ToolResult;
			if (!ParseToolResult(Value.at("tool_results")[Index], ToolResult, "tool_results[" + std::to_string(Index) + "]", OutError))
			{
				return false;
			}
			if (ToolResult.RequestId != Result.RequestId ||
				ToolResult.BeforeRevision != Result.BeforeRevision ||
				ToolResult.AfterRevision != Result.AfterRevision)
			{
				SetError(OutError, "CORRELATION_MISMATCH", "ToolResult does not correlate with its transaction");
				return false;
			}
			Result.ToolResults.push_back(std::move(ToolResult));
		}
		if (Value.at("undo_token").is_null())
		{
			Result.UndoToken.reset();
		}
		else
		{
			std::string UndoToken;
			if (!ReadUuid(Value, "undo_token", UndoToken, "", OutError))
			{
				return false;
			}
			Result.UndoToken = std::move(UndoToken);
		}
		if (Value.contains("failed_tool_call_index") && !Value.at("failed_tool_call_index").is_null())
		{
			std::uint64_t Index = 0;
			if (!ReadSafeInteger(Value, "failed_tool_call_index", Index, "", OutError))
			{
				return false;
			}
			Result.FailedToolCallIndex = static_cast<std::size_t>(Index);
		}
		if (Request)
		{
			if (!ValidateCorrelation(
				Result.RequestId,
				Result.SessionId,
				Result.WorldId,
				Request->RequestId,
				Request->SessionId,
				Request->WorldId,
				OutError))
			{
				return false;
			}
			std::unordered_map<std::string, std::size_t> CallIndex;
			for (std::size_t Index = 0; Index < Request->ToolCalls.size(); ++Index)
			{
				CallIndex.emplace(Request->ToolCalls[Index].ToolCallId, Index);
			}
			std::unordered_set<std::string> Seen;
			std::size_t Previous = 0;
			bool bHasPrevious = false;
			for (const FWorldAdapterToolResult& ToolResult : Result.ToolResults)
			{
				if (!ToolResult.ToolCallId || !CallIndex.contains(*ToolResult.ToolCallId) || !Seen.emplace(*ToolResult.ToolCallId).second)
				{
					SetError(OutError, "CORRELATION_MISMATCH", "ToolResult cannot be correlated to a requested ToolCall");
					return false;
				}
				const std::size_t Current = CallIndex.at(*ToolResult.ToolCallId);
				if (bHasPrevious && Current < Previous)
				{
					SetError(OutError, "CORRELATION_MISMATCH", "ToolResults do not preserve request order");
					return false;
				}
				Previous = Current;
				bHasPrevious = true;
			}
			if ((Result.bOk && Result.ToolResults.size() != Request->ToolCalls.size()) ||
				(!Result.bOk && Result.ToolResults.empty()))
			{
				return FailField(OutError, "tool_results", "ToolResult count does not match transaction success semantics");
			}
		}
		if (OutResponse)
		{
			*OutResponse = std::move(Result);
		}
		return true;
	}
	catch (...)
	{
		SetError(OutError, "RESPONSE_INVALID", "Execute response validation failed safely");
		return false;
	}
}

bool FWorldAdapterJsonProtocol::ValidateUndoResponse(
	const FWorldAdapterJson& Value,
	const FWorldAdapterUndoRequest* Request,
	FWorldAdapterUndoResponse* OutResponse,
	FWorldAdapterError& OutError) noexcept
{
	try
	{
		if (!ValidateJsonValue(Value, "undo response", 0, OutError) ||
			!RequireKnownFields(
				Value,
				{
					"ok", "adapter_protocol_version", "request_id", "session_id", "world_id", "before_revision",
					"after_revision", "replayed", "changes", "undo_token", "error", "data", "adapter_metadata",
				},
				"undo response",
				OutError) ||
			!RequireFields(
				Value,
				{
					"ok", "adapter_protocol_version", "request_id", "session_id", "world_id", "before_revision",
					"after_revision", "replayed", "changes", "undo_token", "error",
				},
				"undo response",
				OutError) ||
			!ValidateProtocolVersion(Value, OutError))
		{
			return false;
		}
		FWorldAdapterUndoResponse Result;
		if (!ReadBool(Value, "ok", Result.bOk, "", OutError) ||
			!ParseBaseIdentity(Value, Result.RequestId, Result.SessionId, Result.WorldId, OutError) ||
			!ReadSafeInteger(Value, "before_revision", Result.BeforeRevision, "", OutError) ||
			!ReadSafeInteger(Value, "after_revision", Result.AfterRevision, "", OutError) ||
			!ReadBool(Value, "replayed", Result.bReplayed, "", OutError) ||
			!ParseChanges(Value.at("changes"), Result.Changes, "changes", OutError) ||
			!ParseError(Value.at("error"), Result.Error, "error", OutError))
		{
			return false;
		}
		if (Result.AfterRevision < Result.BeforeRevision)
		{
			return FailField(OutError, "after_revision", "World adapter response revision moved backward");
		}
		if (Value.at("undo_token").is_null())
		{
			Result.UndoToken.reset();
		}
		else
		{
			std::string UndoToken;
			if (!ReadUuid(Value, "undo_token", UndoToken, "", OutError))
			{
				return false;
			}
			Result.UndoToken = std::move(UndoToken);
		}
		Result.Data = Value.value("data", FWorldAdapterJson());
		if (Request && !ValidateCorrelation(
			Result.RequestId,
			Result.SessionId,
			Result.WorldId,
			Request->RequestId,
			Request->SessionId,
			Request->WorldId,
			OutError))
		{
			return false;
		}
		if (OutResponse)
		{
			*OutResponse = std::move(Result);
		}
		return true;
	}
	catch (...)
	{
		SetError(OutError, "RESPONSE_INVALID", "Undo response validation failed safely");
		return false;
	}
}

FWorldAdapterJson FWorldAdapterJsonProtocol::ToJson(const FWorldAdapterCapabilities& Value)
{
	return {
		{ "supports_atomic_transactions", Value.bSupportsAtomicTransactions },
		{ "supports_dry_run", Value.bSupportsDryRun },
		{ "supports_undo", Value.bSupportsUndo },
		{ "supports_idempotency", Value.bSupportsIdempotency },
		{ "max_tool_calls", Value.MaxToolCalls },
		{ "supported_tools", Value.SupportedTools },
	};
}

FWorldAdapterJson FWorldAdapterJsonProtocol::ToJson(const FWorldAdapterError& Value)
{
	return {
		{ "code", Value.Code },
		{ "message", Value.Message },
		{ "details", Value.Details },
		{ "retryable", Value.bRetryable },
	};
}

FWorldAdapterJson FWorldAdapterJsonProtocol::ToJson(const FWorldAdapterEntity& Value)
{
	return {
		{ "entity_id", Value.EntityId },
		{ "generation", Value.Generation },
		{ "name", Value.Name },
		{ "entity_type", Value.EntityType },
		{ "primitive_type", Value.PrimitiveType },
		{ "transform", TransformToJson(Value.Transform) },
		{ "properties", PropertiesToJson(Value.Properties) },
	};
}

FWorldAdapterJson FWorldAdapterJsonProtocol::ToJson(const FWorldAdapterHealthResponse& Value)
{
	return {
		{ "ok", Value.bOk },
		{ "adapter_protocol_version", WorldAdapterProtocolVersion },
		{ "server_name", Value.ServerName },
		{ "server_version", Value.ServerVersion },
		{ "capabilities", ToJson(Value.Capabilities) },
		{ "error", Value.Error ? ToJson(*Value.Error) : FWorldAdapterJson(nullptr) },
	};
}

FWorldAdapterJson FWorldAdapterJsonProtocol::ToJson(const FWorldAdapterSnapshotResponse& Value)
{
	FWorldAdapterJson Entities = FWorldAdapterJson::array();
	for (const FWorldAdapterEntity& Entity : Value.Entities)
	{
		Entities.push_back(ToJson(Entity));
	}
	return {
		{ "ok", Value.bOk },
		{ "adapter_protocol_version", WorldAdapterProtocolVersion },
		{ "request_id", Value.RequestId },
		{ "session_id", Value.SessionId },
		{ "world_id", Value.WorldId },
		{ "world_revision", Value.WorldRevision },
		{ "timestamp_ms", Value.TimestampMs },
		{ "capabilities", ToJson(Value.Capabilities) },
		{ "entities", std::move(Entities) },
		{ "history", Value.History },
		{ "error", Value.Error ? ToJson(*Value.Error) : FWorldAdapterJson(nullptr) },
	};
}

FWorldAdapterJson FWorldAdapterJsonProtocol::ToJson(const FWorldAdapterExecuteResponse& Value)
{
	FWorldAdapterJson ToolResults = FWorldAdapterJson::array();
	for (const FWorldAdapterToolResult& ToolResult : Value.ToolResults)
	{
		ToolResults.push_back(ToolResultToJson(ToolResult));
	}
	FWorldAdapterJson Changes = FWorldAdapterJson::array();
	for (const FWorldAdapterChange& Change : Value.Changes)
	{
		Changes.push_back(ChangeToJson(Change));
	}
	return {
		{ "ok", Value.bOk },
		{ "adapter_protocol_version", WorldAdapterProtocolVersion },
		{ "request_id", Value.RequestId },
		{ "session_id", Value.SessionId },
		{ "world_id", Value.WorldId },
		{ "before_revision", Value.BeforeRevision },
		{ "after_revision", Value.AfterRevision },
		{ "replayed", Value.bReplayed },
		{ "tool_results", std::move(ToolResults) },
		{ "changes", std::move(Changes) },
		{ "undo_token", Value.UndoToken ? FWorldAdapterJson(*Value.UndoToken) : FWorldAdapterJson(nullptr) },
		{ "error", Value.Error ? ToJson(*Value.Error) : FWorldAdapterJson(nullptr) },
		{ "failed_tool_call_index", Value.FailedToolCallIndex ? FWorldAdapterJson(*Value.FailedToolCallIndex) : FWorldAdapterJson(nullptr) },
	};
}

FWorldAdapterJson FWorldAdapterJsonProtocol::ToJson(const FWorldAdapterUndoResponse& Value)
{
	FWorldAdapterJson Changes = FWorldAdapterJson::array();
	for (const FWorldAdapterChange& Change : Value.Changes)
	{
		Changes.push_back(ChangeToJson(Change));
	}
	return {
		{ "ok", Value.bOk },
		{ "adapter_protocol_version", WorldAdapterProtocolVersion },
		{ "request_id", Value.RequestId },
		{ "session_id", Value.SessionId },
		{ "world_id", Value.WorldId },
		{ "before_revision", Value.BeforeRevision },
		{ "after_revision", Value.AfterRevision },
		{ "replayed", Value.bReplayed },
		{ "changes", std::move(Changes) },
		{ "undo_token", Value.UndoToken ? FWorldAdapterJson(*Value.UndoToken) : FWorldAdapterJson(nullptr) },
		{ "error", Value.Error ? ToJson(*Value.Error) : FWorldAdapterJson(nullptr) },
		{ "data", Value.Data },
	};
}

std::string FWorldAdapterJsonProtocol::Serialize(const FWorldAdapterJson& Value)
{
	return Value.dump();
}

std::string FWorldAdapterJsonProtocol::CanonicalFingerprint(const FWorldAdapterJson& Value) noexcept
{
	try
	{
		const std::string Canonical = Value.dump();
		std::uint64_t First = 14695981039346656037ULL;
		std::uint64_t Second = 1099511628211ULL;
		for (const unsigned char Byte : Canonical)
		{
			First ^= Byte;
			First *= 1099511628211ULL;
			Second ^= static_cast<std::uint64_t>(Byte) + 0x9e3779b97f4a7c15ULL;
			Second *= 14029467366897019727ULL;
		}
		std::ostringstream Stream;
		Stream << std::hex << std::setfill('0')
			<< std::setw(16) << First
			<< std::setw(16) << Second
			<< std::setw(16) << static_cast<std::uint64_t>(Canonical.size());
		return Stream.str();
	}
	catch (...)
	{
		return {};
	}
}

bool FWorldAdapterJsonProtocol::IsUuid(std::string_view Value) noexcept
{
	if (Value.size() != 36 || Value[8] != '-' || Value[13] != '-' || Value[18] != '-' || Value[23] != '-')
	{
		return false;
	}
	auto IsHex = [](char Character)
	{
		return (Character >= '0' && Character <= '9') ||
			(Character >= 'a' && Character <= 'f') ||
			(Character >= 'A' && Character <= 'F');
	};
	for (std::size_t Index = 0; Index < Value.size(); ++Index)
	{
		if (Index == 8 || Index == 13 || Index == 18 || Index == 23)
		{
			continue;
		}
		if (!IsHex(Value[Index]))
		{
			return false;
		}
	}
	if (Value[14] < '1' || Value[14] > '8')
	{
		return false;
	}
	const char Variant = Value[19];
	return Variant == '8' || Variant == '9' || Variant == 'a' || Variant == 'A' || Variant == 'b' || Variant == 'B';
}

} // namespace Maho
