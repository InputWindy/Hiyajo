#include "TestFramework.h"

#include <WorldAdapter/Protocol/Json.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace
{

using Maho::FWorldAdapterError;
using Maho::FWorldAdapterExecuteRequest;
using Maho::FWorldAdapterJson;
using Maho::FWorldAdapterJsonProtocol;
using Maho::FWorldAdapterSnapshotRequest;
using Maho::FWorldAdapterUndoRequest;

const std::filesystem::path FixtureRoot(MAHO_WORLD_ADAPTER_FIXTURE_ROOT);

std::string ReadText(const std::filesystem::path& Path)
{
	std::ifstream Input(Path, std::ios::binary);
	if (!Input)
	{
		throw std::runtime_error("Unable to open fixture: " + Path.string());
	}
	return std::string(std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>());
}

FWorldAdapterJson ReadFixture(const std::string& Name)
{
	FWorldAdapterJson Result;
	FWorldAdapterError Error;
	const std::string Text = ReadText(FixtureRoot / Name);
	if (!FWorldAdapterJsonProtocol::ParseJson(Text, Result, Error))
	{
		throw std::runtime_error(Name + " failed JSON parsing: " + Error.Message);
	}
	return Result;
}

FWorldAdapterExecuteRequest RequestForResponse(
	const FWorldAdapterJson& Response,
	const std::string& ToolName,
	const FWorldAdapterJson& Args,
	bool bDryRun = false,
	bool bAtomic = false)
{
	FWorldAdapterExecuteRequest Request;
	Request.RequestId = Response.at("request_id").get<std::string>();
	Request.SessionId = Response.at("session_id").get<std::string>();
	Request.WorldId = Response.at("world_id").get<std::string>();
	Request.ExpectedRevision = Response.at("before_revision").get<std::uint64_t>();
	Request.bDryRun = bDryRun;
	Request.bAtomic = bAtomic;
	Maho::FWorldAdapterToolCall Call;
	Call.ToolCallId = Response.at("tool_results").at(0).at("tool_call_id").get<std::string>();
	Call.ToolName = ToolName;
	Call.Args = Args;
	Request.ToolCalls.push_back(std::move(Call));
	return Request;
}

} // namespace

void RunProtocolTests(FTestRunner& Runner)
{
	Runner.Run("Golden fixture inventory", [&Runner]()
	{
		const std::vector<std::string> Expected =
		{
			"execute-dry-run-unsupported-error.json",
			"execute-spawn-request.json",
			"execute-spawn-success.json",
			"execute-too-many-tools-error.json",
			"execute-transform-request.json",
			"execute-transform-success.json",
			"execute-unsupported-tool-error.json",
			"health-full.json",
			"health-invalid-undo-dependency.json",
			"health-minimal.json",
			"health-unknown-tool.json",
			"revision-conflict-error.json",
			"snapshot-empty.json",
			"snapshot-one-entity.json",
			"undo-unsupported-error.json",
		};
		std::vector<std::string> Actual;
		for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(FixtureRoot))
		{
			if (Entry.path().extension() == ".json")
			{
				Actual.push_back(Entry.path().filename().string());
			}
		}
		std::sort(Actual.begin(), Actual.end());
		Runner.Check(Actual == Expected, "Golden fixture inventory differs from Node inventory");
	});

	Runner.Run("Health fixtures", [&Runner]()
	{
		FWorldAdapterError Error;
		Runner.Check(FWorldAdapterJsonProtocol::ValidateHealthResponse(ReadFixture("health-full.json"), nullptr, Error), "full health fixture rejected");
		Runner.Check(FWorldAdapterJsonProtocol::ValidateHealthResponse(ReadFixture("health-minimal.json"), nullptr, Error), "minimal health fixture rejected");
		Runner.Check(!FWorldAdapterJsonProtocol::ValidateHealthResponse(ReadFixture("health-invalid-undo-dependency.json"), nullptr, Error), "invalid undo dependency accepted");
		Runner.Check(!FWorldAdapterJsonProtocol::ValidateHealthResponse(ReadFixture("health-unknown-tool.json"), nullptr, Error), "unknown capability tool accepted");
	});

	Runner.Run("Snapshot fixtures", [&Runner]()
	{
		for (const std::string Name : { "snapshot-empty.json", "snapshot-one-entity.json" })
		{
			const FWorldAdapterJson Fixture = ReadFixture(Name);
			FWorldAdapterSnapshotRequest Request;
			Request.RequestId = Fixture.at("request_id").get<std::string>();
			Request.SessionId = Fixture.at("session_id").get<std::string>();
			Request.WorldId = Fixture.at("world_id").get<std::string>();
			FWorldAdapterError Error;
			Runner.Check(FWorldAdapterJsonProtocol::ValidateSnapshotResponse(Fixture, &Request, nullptr, Error), Name + " rejected: " + Error.Message);
		}
	});

	Runner.Run("Spawn request and response fixtures", [&Runner]()
	{
		FWorldAdapterExecuteRequest Request;
		FWorldAdapterError Error;
		Runner.Check(FWorldAdapterJsonProtocol::ParseExecuteRequest(ReadText(FixtureRoot / "execute-spawn-request.json"), Request, Error), Error.Message);
		Runner.Check(FWorldAdapterJsonProtocol::ValidateExecuteResponse(ReadFixture("execute-spawn-success.json"), &Request, nullptr, Error), Error.Message);
	});

	Runner.Run("Transform request and response fixtures", [&Runner]()
	{
		FWorldAdapterExecuteRequest Request;
		FWorldAdapterError Error;
		Runner.Check(FWorldAdapterJsonProtocol::ParseExecuteRequest(ReadText(FixtureRoot / "execute-transform-request.json"), Request, Error), Error.Message);
		Runner.Check(FWorldAdapterJsonProtocol::ValidateExecuteResponse(ReadFixture("execute-transform-success.json"), &Request, nullptr, Error), Error.Message);
	});

	Runner.Run("Execute error fixtures", [&Runner]()
	{
		struct FCase
		{
			const char* Name;
			const char* Tool;
			FWorldAdapterJson Args;
			bool bDryRun;
			bool bAtomic;
		};
		const std::vector<FCase> Cases =
		{
			{ "execute-unsupported-tool-error.json", "entity.destroy", { { "entity_id", "55555555-5555-4555-8555-555555555555" } }, false, false },
			{ "execute-dry-run-unsupported-error.json", "world.get_summary", FWorldAdapterJson::object(), true, false },
			{ "revision-conflict-error.json", "entity.set_transform", { { "entity_id", "55555555-5555-4555-8555-555555555555" }, { "transform", { { "position", { 10, 20, 30 } } } } }, false, false },
		};
		for (const FCase& Item : Cases)
		{
			const FWorldAdapterJson Fixture = ReadFixture(Item.Name);
			FWorldAdapterExecuteRequest Request = RequestForResponse(Fixture, Item.Tool, Item.Args, Item.bDryRun, Item.bAtomic);
			FWorldAdapterError Error;
			Runner.Check(FWorldAdapterJsonProtocol::ValidateExecuteResponse(Fixture, &Request, nullptr, Error), std::string(Item.Name) + " rejected: " + Error.Message);
		}
		FWorldAdapterError Error;
		Runner.Check(FWorldAdapterJsonProtocol::ValidateExecuteResponse(ReadFixture("execute-too-many-tools-error.json"), nullptr, nullptr, Error), "too-many fixture rejected");
	});

	Runner.Run("Undo fixture", [&Runner]()
	{
		const FWorldAdapterJson Fixture = ReadFixture("undo-unsupported-error.json");
		FWorldAdapterUndoRequest Request;
		Request.RequestId = Fixture.at("request_id").get<std::string>();
		Request.SessionId = Fixture.at("session_id").get<std::string>();
		Request.WorldId = Fixture.at("world_id").get<std::string>();
		Request.ExpectedRevision = Fixture.at("before_revision").get<std::uint64_t>();
		FWorldAdapterError Error;
		Runner.Check(FWorldAdapterJsonProtocol::ValidateUndoResponse(Fixture, &Request, nullptr, Error), Error.Message);
	});

	Runner.Run("Unknown fields and invalid types", [&Runner]()
	{
		FWorldAdapterJson Request = ReadFixture("execute-spawn-request.json");
		Request["unexpected"] = true;
		FWorldAdapterExecuteRequest Parsed;
		FWorldAdapterError Error;
		Runner.Check(!FWorldAdapterJsonProtocol::ParseExecuteRequest(Request.dump(), Parsed, Error), "unknown request field accepted");
		Request.erase("unexpected");
		Request["expected_revision"] = "zero";
		Runner.Check(!FWorldAdapterJsonProtocol::ParseExecuteRequest(Request.dump(), Parsed, Error), "invalid revision type accepted");
	});

	Runner.Run("UUID and transform bounds", [&Runner]()
	{
		FWorldAdapterJson Request = ReadFixture("execute-transform-request.json");
		Request["request_id"] = "not-a-uuid";
		FWorldAdapterExecuteRequest Parsed;
		FWorldAdapterError Error;
		Runner.Check(!FWorldAdapterJsonProtocol::ParseExecuteRequest(Request.dump(), Parsed, Error), "invalid UUID accepted");
		Request = ReadFixture("execute-transform-request.json");
		Request["tool_calls"][0]["args"]["transform"]["position"][0] = std::numeric_limits<double>::infinity();
		Runner.Check(!FWorldAdapterJsonProtocol::ParseExecuteRequest(Request.dump(), Parsed, Error), "non-finite transform accepted");
	});

	Runner.Run("Protocol size limits", [&Runner]()
	{
		FWorldAdapterJson Request = ReadFixture("execute-spawn-request.json");
		Request["tool_calls"][0]["args"]["name"] = std::string(129, 'x');
		FWorldAdapterExecuteRequest Parsed;
		FWorldAdapterError Error;
		Runner.Check(!FWorldAdapterJsonProtocol::ParseExecuteRequest(Request.dump(), Parsed, Error), "oversized name accepted");
		std::string Oversized(Maho::WorldAdapterRequestBodyLimit + 1, 'x');
		FWorldAdapterJson Value;
		Runner.Check(!FWorldAdapterJsonProtocol::ParseJson(Oversized, Value, Error), "oversized body accepted");
		Request = ReadFixture("execute-spawn-request.json");
		const FWorldAdapterJson Call = Request["tool_calls"][0];
		Request["tool_calls"] = FWorldAdapterJson::array();
		for (int Index = 0; Index < 1001; ++Index)
		{
			Request["tool_calls"].push_back(Call);
		}
		Runner.Check(!FWorldAdapterJsonProtocol::ParseExecuteRequest(Request.dump(), Parsed, Error), "oversized tool array accepted");
	});

	Runner.Run("Unknown tool and capability dependencies", [&Runner]()
	{
		FWorldAdapterJson Request = ReadFixture("execute-spawn-request.json");
		Request["tool_calls"][0]["tool_name"] = "entity.teleport";
		FWorldAdapterExecuteRequest Parsed;
		FWorldAdapterError Error;
		Runner.Check(!FWorldAdapterJsonProtocol::ParseExecuteRequest(Request.dump(), Parsed, Error), "unknown ToolCall accepted");
		Runner.Check(!FWorldAdapterJsonProtocol::ValidateHealthResponse(ReadFixture("health-invalid-undo-dependency.json"), nullptr, Error), "invalid capability dependency accepted");
	});

	Runner.Run("Canonical payload fingerprint", [&Runner]()
	{
		const FWorldAdapterJson First = FWorldAdapterJson::parse(R"({"b":2,"a":{"y":4,"x":3}})");
		const FWorldAdapterJson Second = FWorldAdapterJson::parse(R"({"a":{"x":3,"y":4},"b":2})");
		const FWorldAdapterJson Different = FWorldAdapterJson::parse(R"({"a":{"x":3,"y":5},"b":2})");
		Runner.Check(FWorldAdapterJsonProtocol::CanonicalFingerprint(First) == FWorldAdapterJsonProtocol::CanonicalFingerprint(Second), "field order changed fingerprint");
		Runner.Check(FWorldAdapterJsonProtocol::CanonicalFingerprint(First) != FWorldAdapterJsonProtocol::CanonicalFingerprint(Different), "different payload shared fingerprint");
	});
}
