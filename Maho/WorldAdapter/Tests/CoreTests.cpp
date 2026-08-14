#include "TestFramework.h"

#include <WorldAdapter/Core/CommandQueue.h>
#include <WorldAdapter/Protocol/Json.h>
#include <WorldAdapter/Stub/StubBackend.h>

#include <atomic>
#include <chrono>
#include <future>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace std::chrono_literals;
using Maho::EWorldAdapterEnqueueResult;
using Maho::EWorldAdapterRequestState;
using Maho::EWorldAdapterWaitResult;
using Maho::FStubWorldBackend;
using Maho::FStubWorldBackendConfig;
using Maho::FWorldAdapterCommandEnvelope;
using Maho::FWorldAdapterCommandQueue;
using Maho::FWorldAdapterCommandResult;
using Maho::FWorldAdapterExecuteRequest;
using Maho::FWorldAdapterExecuteResponse;
using Maho::FWorldAdapterJson;
using Maho::FWorldAdapterJsonProtocol;
using Maho::FWorldAdapterRequestState;
using Maho::FWorldAdapterToolCall;

const std::string SessionId = "22222222-2222-4222-8222-222222222222";
const std::string WorldId = "33333333-3333-4333-8333-333333333333";

std::string IndexedUuid(std::uint64_t Index)
{
	std::ostringstream Stream;
	Stream << "aaaaaaaa-aaaa-4aaa-8aaa-" << std::hex << std::setw(12) << std::setfill('0') << Index;
	return Stream.str();
}

FWorldAdapterExecuteRequest MakeRequest(
	std::string ToolName,
	FWorldAdapterJson Args,
	std::uint64_t ExpectedRevision,
	std::string RequestId,
	std::string InSessionId = SessionId,
	std::string InWorldId = WorldId)
{
	FWorldAdapterExecuteRequest Request;
	Request.RequestId = std::move(RequestId);
	Request.SessionId = std::move(InSessionId);
	Request.WorldId = std::move(InWorldId);
	Request.ExpectedRevision = ExpectedRevision;
	FWorldAdapterToolCall Call;
	Call.ToolCallId = IndexedUuid(ExpectedRevision + 500);
	Call.ToolName = std::move(ToolName);
	Call.Args = std::move(Args);
	Request.ToolCalls.push_back(Call);
	Request.CanonicalPayload = {
		{ "adapter_protocol_version", "1.0" },
		{ "request_id", Request.RequestId },
		{ "session_id", Request.SessionId },
		{ "world_id", Request.WorldId },
		{ "expected_revision", Request.ExpectedRevision },
		{ "dry_run", Request.bDryRun },
		{ "atomic", Request.bAtomic },
		{
			"tool_calls",
			FWorldAdapterJson::array({
				{
					{ "tool_call_id", Call.ToolCallId },
					{ "tool_name", Call.ToolName },
					{ "args", Call.Args },
				},
			}),
		},
	};
	return Request;
}

FWorldAdapterExecuteResponse PumpRequest(
	FWorldAdapterCommandQueue& Queue,
	FStubWorldBackend& Backend,
	FWorldAdapterExecuteRequest Request)
{
	auto State = std::make_shared<FWorldAdapterRequestState>();
	if (Queue.Enqueue({ std::move(Request), State }) != EWorldAdapterEnqueueResult::Accepted)
	{
		throw std::runtime_error("queue rejected request");
	}
	if (!Queue.PumpOne(Backend))
	{
		throw std::runtime_error("queue did not pump request");
	}
	FWorldAdapterCommandResult Result;
	if (State->WaitFor(100ms, Result) != EWorldAdapterWaitResult::Completed)
	{
		throw std::runtime_error("request did not complete");
	}
	return std::get<FWorldAdapterExecuteResponse>(Result.Response);
}

} // namespace

void RunCoreTests(FTestRunner& Runner)
{
	Runner.Run("Stub initial revision and summary", [&Runner]()
	{
		FStubWorldBackend Backend;
		Runner.Check(Backend.GetRevision(SessionId, WorldId) == 0, "initial revision is not zero");
		const FWorldAdapterExecuteResponse Summary = Backend.Execute(MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(1)));
		Runner.Check(Summary.bOk && Summary.AfterRevision == 0, "summary changed revision");
		Runner.Check(Summary.ToolResults[0].Data.at("entity_count") == 0, "summary entity count is wrong");
	});

	Runner.Run("Stub spawn and UUID", [&Runner]()
	{
		FStubWorldBackend Backend;
		const FWorldAdapterExecuteResponse Spawn = Backend.Execute(MakeRequest("entity.spawn_primitive", { { "primitive_type", "cube" }, { "name", "CoreCube" } }, 0, IndexedUuid(2)));
		const std::string EntityId = Spawn.ToolResults[0].Data.at("entity").at("entity_id").get<std::string>();
		Runner.Check(Spawn.bOk && Spawn.BeforeRevision == 0 && Spawn.AfterRevision == 1, "spawn revision is wrong");
		Runner.Check(FWorldAdapterJsonProtocol::IsUuid(EntityId), "spawn entity_id is not a UUID");
		Runner.Check(Spawn.ToolResults[0].Data.at("entity").at("generation") == 1, "spawn generation is not one");
	});

	Runner.Run("Stub transform and snapshot", [&Runner]()
	{
		FStubWorldBackend Backend;
		const FWorldAdapterExecuteResponse Spawn = Backend.Execute(MakeRequest("entity.spawn_primitive", { { "primitive_type", "sphere" } }, 0, IndexedUuid(3)));
		const std::string EntityId = Spawn.ToolResults[0].Data.at("entity").at("entity_id").get<std::string>();
		const FWorldAdapterExecuteResponse Transform = Backend.Execute(MakeRequest("entity.set_transform", { { "entity_id", EntityId }, { "transform", { { "position", { 1, 2, 3 } } } } }, 1, IndexedUuid(4)));
		Maho::FWorldAdapterSnapshotRequest SnapshotRequest{ IndexedUuid(5), SessionId, WorldId };
		const auto Snapshot = Backend.GetSnapshot(SnapshotRequest);
		Runner.Check(Transform.bOk && Transform.AfterRevision == 2, "transform revision is wrong");
		Runner.Check(Snapshot.WorldRevision == 2 && Snapshot.Entities.size() == 1, "snapshot state is wrong");
		Runner.Check(Snapshot.Entities[0].Transform.Position == std::array<double, 3>{ 1.0, 2.0, 3.0 }, "snapshot transform is wrong");
	});

	Runner.Run("Stub failures preserve revision", [&Runner]()
	{
		FStubWorldBackend Backend;
		const auto Missing = Backend.Execute(MakeRequest("entity.set_transform", { { "entity_id", IndexedUuid(900) }, { "transform", { { "position", { 1, 2, 3 } } } } }, 0, IndexedUuid(6)));
		const auto Conflict = Backend.Execute(MakeRequest("world.get_summary", FWorldAdapterJson::object(), 1, IndexedUuid(7)));
		const auto Unsupported = Backend.Execute(MakeRequest("entity.destroy", { { "entity_id", IndexedUuid(900) } }, 0, IndexedUuid(8)));
		Runner.Check(!Missing.bOk && Missing.Error->Code == "ENTITY_NOT_FOUND", "entity-not-found failure is wrong");
		Runner.Check(!Conflict.bOk && Conflict.Error->Code == "REVISION_CONFLICT", "revision-conflict failure is wrong");
		Runner.Check(!Unsupported.bOk && Unsupported.Error->Code == "INVALID_REQUEST", "unsupported-tool failure is wrong");
		Runner.Check(Backend.GetRevision(SessionId, WorldId) == 0, "failure changed revision");
	});

	Runner.Run("Stub undo unavailable", [&Runner]()
	{
		FStubWorldBackend Backend;
		Maho::FWorldAdapterUndoRequest Request;
		Request.RequestId = IndexedUuid(9);
		Request.SessionId = SessionId;
		Request.WorldId = WorldId;
		const auto Undo = Backend.Undo(Request);
		Runner.Check(!Undo.bOk && Undo.Error->Code == "UNDO_NOT_AVAILABLE", "undo did not return UNDO_NOT_AVAILABLE");
		Runner.Check(Backend.GetRevision(SessionId, WorldId) == 0, "undo changed revision");
	});

	Runner.Run("Idempotent replay and payload mismatch", [&Runner]()
	{
		FStubWorldBackend Backend;
		const FWorldAdapterExecuteRequest Request = MakeRequest("entity.spawn_primitive", { { "primitive_type", "cube" } }, 0, IndexedUuid(10));
		const auto First = Backend.Execute(Request);
		const auto Replay = Backend.Execute(Request);
		FWorldAdapterExecuteRequest Mismatch = Request;
		Mismatch.ToolCalls[0].Args["name"] = "Different";
		Mismatch.CanonicalPayload["tool_calls"][0]["args"]["name"] = "Different";
		const auto Conflict = Backend.Execute(Mismatch);
		Runner.Check(First.bOk && Replay.bOk && Replay.bReplayed, "same payload did not replay");
		Runner.Check(First.AfterRevision == Replay.AfterRevision && Backend.GetRevision(SessionId, WorldId) == 1, "replay advanced revision");
		Runner.Check(!Conflict.bOk && Conflict.Error->Code == "REQUEST_ID_CONFLICT", "payload mismatch did not conflict");
		Runner.Check(Backend.GetExecutionCount() == 1, "replay executed twice");
	});

	Runner.Run("Idempotent failure replay", [&Runner]()
	{
		FStubWorldBackend Backend;
		const FWorldAdapterExecuteRequest Request = MakeRequest(
			"entity.spawn_primitive",
			{ { "primitive_type", "invalid" } },
			0,
			IndexedUuid(101));
		const auto First = Backend.Execute(Request);
		const auto Replay = Backend.Execute(Request);
		Runner.Check(!First.bOk && First.Error->Code == "INVALID_ARGUMENT", "invalid request failure is wrong");
		Runner.Check(!Replay.bOk && Replay.bReplayed && Replay.Error->Code == "INVALID_ARGUMENT", "failed request did not replay");
		Runner.Check(Backend.GetExecutionCount() == 1 && Backend.GetRevision(SessionId, WorldId) == 0, "failed replay executed twice or changed revision");
	});

	Runner.Run("Idempotency scope isolation", [&Runner]()
	{
		FStubWorldBackend Backend;
		const std::string RequestId = IndexedUuid(11);
		const std::string OtherSession = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
		const std::string OtherWorld = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
		const auto First = Backend.Execute(MakeRequest("entity.spawn_primitive", { { "primitive_type", "cube" } }, 0, RequestId));
		const auto Second = Backend.Execute(MakeRequest("entity.spawn_primitive", { { "primitive_type", "sphere" } }, 0, RequestId, OtherSession, WorldId));
		const auto Third = Backend.Execute(MakeRequest("entity.spawn_primitive", { { "primitive_type", "plane" } }, 0, RequestId, SessionId, OtherWorld));
		Runner.Check(First.bOk && Second.bOk && Third.bOk, "isolated scopes did not execute");
		Runner.Check(Backend.GetExecutionCount() == 3, "isolated scopes were treated as replay");
	});

	Runner.Run("Idempotency cache eviction", [&Runner]()
	{
		FStubWorldBackend Backend({ 2, 0ms });
		const auto First = MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(12));
		(void)Backend.Execute(First);
		(void)Backend.Execute(MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(13)));
		(void)Backend.Execute(MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(14)));
		const auto Evicted = Backend.Execute(First);
		Runner.Check(Evicted.bOk && !Evicted.bReplayed, "evicted request was replayed");
		Runner.Check(Backend.GetExecutionCount() == 4, "evicted request was not executed again");
	});

	Runner.Run("Queue enqueue and pump", [&Runner]()
	{
		FStubWorldBackend Backend;
		FWorldAdapterCommandQueue Queue(4);
		const auto Response = PumpRequest(Queue, Backend, MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(15)));
		Runner.Check(Response.bOk && Queue.GetSize() == 0, "queue pump failed");
		Runner.Check(Backend.GetLastExecutionThreadId() == std::this_thread::get_id(), "backend ran off pump thread");
	});

	Runner.Run("Queue full backpressure", [&Runner]()
	{
		FWorldAdapterCommandQueue Queue(1);
		auto First = std::make_shared<FWorldAdapterRequestState>();
		auto Second = std::make_shared<FWorldAdapterRequestState>();
		Runner.Check(Queue.Enqueue({ MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(16)), First }) == EWorldAdapterEnqueueResult::Accepted, "first request rejected");
		Runner.Check(Queue.Enqueue({ MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(17)), Second }) == EWorldAdapterEnqueueResult::Full, "full queue did not backpressure");
	});

	Runner.Run("Queue multiple producers", [&Runner]()
	{
		FStubWorldBackend Backend;
		FWorldAdapterCommandQueue Queue(64);
		std::vector<std::shared_ptr<FWorldAdapterRequestState>> States(32);
		std::vector<std::thread> Producers;
		for (std::size_t Producer = 0; Producer < 4; ++Producer)
		{
			Producers.emplace_back([Producer, &Queue, &States]()
			{
				for (std::size_t Local = 0; Local < 8; ++Local)
				{
					const std::size_t Index = Producer * 8 + Local;
					States[Index] = std::make_shared<FWorldAdapterRequestState>();
					(void)Queue.Enqueue({ MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(100 + Index)), States[Index] });
				}
			});
		}
		for (std::thread& Producer : Producers)
		{
			Producer.join();
		}
		Runner.Check(Queue.GetSize() == 32, "producer requests were lost");
		Runner.Check(Queue.Pump(Backend, 64) == 32, "not all producer requests pumped");
		Runner.Check(Backend.GetExecutionCount() == 32, "producer commands did not execute once");
	});

	Runner.Run("Queued timeout is skipped", [&Runner]()
	{
		FStubWorldBackend Backend;
		FWorldAdapterCommandQueue Queue(2);
		auto State = std::make_shared<FWorldAdapterRequestState>();
		(void)Queue.Enqueue({ MakeRequest("entity.spawn_primitive", { { "primitive_type", "cube" } }, 0, IndexedUuid(18)), State });
		FWorldAdapterCommandResult Result;
		Runner.Check(State->WaitFor(1ms, Result) == EWorldAdapterWaitResult::TimedOut, "queued request did not time out");
		Runner.Check(Queue.PumpOne(Backend), "timed-out queue entry was not drained");
		Runner.Check(!State->WasExecutionStarted() && Backend.GetRevision(SessionId, WorldId) == 0, "timed-out queued request executed");
	});

	Runner.Run("Queue shutdown cancellation and waiter wakeup", [&Runner]()
	{
		FWorldAdapterCommandQueue Queue(2);
		auto State = std::make_shared<FWorldAdapterRequestState>();
		(void)Queue.Enqueue({ MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(19)), State });
		auto Waiter = std::async(std::launch::async, [State]()
		{
			FWorldAdapterCommandResult Result;
			return State->WaitFor(5s, Result);
		});
		Queue.BeginShutdown();
		Queue.BeginShutdown();
		Runner.Check(Waiter.get() == EWorldAdapterWaitResult::Cancelled, "shutdown did not wake waiter");
		Runner.Check(Queue.Enqueue({ MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(20)), std::make_shared<FWorldAdapterRequestState>() }) == EWorldAdapterEnqueueResult::ShuttingDown, "shutdown queue accepted request");
	});

	Runner.Run("Executing timeout can complete authoritatively", [&Runner]()
	{
		auto State = std::make_shared<FWorldAdapterRequestState>();
		Runner.Check(State->TryMarkExecuting(), "request did not enter executing state");
		std::thread Completer([State]()
		{
			std::this_thread::sleep_for(20ms);
			Maho::FWorldAdapterSnapshotResponse Response;
			Response.RequestId = IndexedUuid(21);
			State->Complete({ std::move(Response) });
		});
		FWorldAdapterCommandResult Result;
		Runner.Check(State->WaitFor(1ms, Result) == EWorldAdapterWaitResult::TimedOut, "executing request did not time out for waiter");
		Completer.join();
		Runner.Check(State->GetState() == EWorldAdapterRequestState::Completed, "authoritative completion was not retained");
	});

	Runner.Run("Concurrent duplicate request executes once", [&Runner]()
	{
		FStubWorldBackend Backend;
		FWorldAdapterCommandQueue Queue(4);
		const FWorldAdapterExecuteRequest Request = MakeRequest("entity.spawn_primitive", { { "primitive_type", "cube" } }, 0, IndexedUuid(22));
		auto First = std::make_shared<FWorldAdapterRequestState>();
		auto Second = std::make_shared<FWorldAdapterRequestState>();
		std::thread ProducerA([&]() { (void)Queue.Enqueue({ Request, First }); });
		std::thread ProducerB([&]() { (void)Queue.Enqueue({ Request, Second }); });
		ProducerA.join();
		ProducerB.join();
		(void)Queue.Pump(Backend, 2);
		FWorldAdapterCommandResult FirstResult;
		FWorldAdapterCommandResult SecondResult;
		Runner.Check(First->WaitFor(100ms, FirstResult) == EWorldAdapterWaitResult::Completed, "first duplicate did not complete");
		Runner.Check(Second->WaitFor(100ms, SecondResult) == EWorldAdapterWaitResult::Completed, "second duplicate did not complete");
		const auto& FirstResponse = std::get<FWorldAdapterExecuteResponse>(FirstResult.Response);
		const auto& SecondResponse = std::get<FWorldAdapterExecuteResponse>(SecondResult.Response);
		Runner.Check(FirstResponse.bReplayed != SecondResponse.bReplayed, "duplicate replay flag is wrong");
		Runner.Check(Backend.GetExecutionCount() == 1 && Backend.GetRevision(SessionId, WorldId) == 1, "duplicate executed more than once");
	});

	Runner.Run("Pump thread ownership", [&Runner]()
	{
		FStubWorldBackend Backend;
		FWorldAdapterCommandQueue Queue(2);
		auto State = std::make_shared<FWorldAdapterRequestState>();
		(void)Queue.Enqueue({ MakeRequest("world.get_summary", FWorldAdapterJson::object(), 0, IndexedUuid(23)), State });
		std::thread::id WorkerId;
		std::thread Worker([&]()
		{
			WorkerId = std::this_thread::get_id();
			(void)Queue.PumpOne(Backend);
		});
		Worker.join();
		Runner.Check(Backend.GetLastExecutionThreadId() == WorkerId && Queue.GetPumpThreadId() == WorkerId, "backend did not run on the sole pump thread");
		Runner.Check(!Queue.PumpOne(Backend), "second pump thread was accepted");
	});
}
