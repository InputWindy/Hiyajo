#include "TestFramework.h"

#include <WorldAdapter/Protocol/Json.h>
#include <WorldAdapter/Service/WorldAdapterService.h>
#include <WorldAdapter/Stub/StubBackend.h>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace std::chrono_literals;
using Maho::FStubWorldBackend;
using Maho::FStubWorldBackendConfig;
using Maho::FWorldAdapterError;
using Maho::FWorldAdapterJson;
using Maho::FWorldAdapterJsonProtocol;
using Maho::FWorldAdapterService;
using Maho::FWorldAdapterServiceConfig;

const std::string SessionId = "22222222-2222-4222-8222-222222222222";
const std::string WorldId = "33333333-3333-4333-8333-333333333333";

struct FHttpResult
{
	int Status = 0;
	std::string Body;
};

class FWinsockScope
{
public:
	FWinsockScope()
	{
		WSADATA Data{};
		bStarted = WSAStartup(MAKEWORD(2, 2), &Data) == 0;
	}

	~FWinsockScope()
	{
		if (bStarted)
		{
			WSACleanup();
		}
	}

	[[nodiscard]] bool IsStarted() const { return bStarted; }

private:
	bool bStarted = false;
};

bool SendAll(SOCKET Socket, const std::string& Data)
{
	std::size_t Offset = 0;
	while (Offset < Data.size())
	{
		const int Sent = send(Socket, Data.data() + Offset, static_cast<int>((std::min)(Data.size() - Offset, std::size_t(65536))), 0);
		if (Sent <= 0)
		{
			return false;
		}
		Offset += static_cast<std::size_t>(Sent);
	}
	return true;
}

SOCKET Connect(std::uint16_t Port)
{
	SOCKET Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (Socket == INVALID_SOCKET)
	{
		throw std::runtime_error("test client socket failed");
	}
	const DWORD Timeout = 10000;
	setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&Timeout), sizeof(Timeout));
	setsockopt(Socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&Timeout), sizeof(Timeout));
	sockaddr_in Address{};
	Address.sin_family = AF_INET;
	Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	Address.sin_port = htons(Port);
	if (connect(Socket, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) == SOCKET_ERROR)
	{
		closesocket(Socket);
		throw std::runtime_error("test client connect failed");
	}
	return Socket;
}

std::string BuildRequest(
	std::uint16_t Port,
	const std::string& Method,
	const std::string& Path,
	const std::string& Body,
	const std::string& ContentType,
	const std::string& Token,
	std::optional<std::size_t> DeclaredContentLength = std::nullopt)
{
	std::ostringstream Stream;
	Stream << Method << ' ' << Path << " HTTP/1.1\r\n"
		<< "Host: 127.0.0.1:" << Port << "\r\n"
		<< "Accept: application/json\r\n"
		<< "Connection: close\r\n";
	if (!ContentType.empty())
	{
		Stream << "Content-Type: " << ContentType << "\r\n";
	}
	if (!Token.empty())
	{
		Stream << "Authorization: Bearer " << Token << "\r\n";
	}
	if (Method == "POST")
	{
		Stream << "Content-Length: " << DeclaredContentLength.value_or(Body.size()) << "\r\n";
	}
	Stream << "\r\n" << Body;
	return Stream.str();
}

FHttpResult HttpRequest(
	std::uint16_t Port,
	std::string Method,
	std::string Path,
	std::string Body = {},
	std::string ContentType = "application/json",
	std::string Token = {},
	std::optional<std::size_t> DeclaredContentLength = std::nullopt)
{
	FWinsockScope Winsock;
	if (!Winsock.IsStarted())
	{
		throw std::runtime_error("test client WSAStartup failed");
	}
	SOCKET Socket = Connect(Port);
	const std::string Request = BuildRequest(Port, Method, Path, Body, ContentType, Token, DeclaredContentLength);
	(void)SendAll(Socket, Request);
	std::string Response;
	std::array<char, 4096> Buffer{};
	for (;;)
	{
		const int Received = recv(Socket, Buffer.data(), static_cast<int>(Buffer.size()), 0);
		if (Received == 0)
		{
			break;
		}
		if (Received < 0)
		{
			if (!Response.empty() && WSAGetLastError() == WSAECONNRESET)
			{
				break;
			}
			closesocket(Socket);
			throw std::runtime_error("test client receive failed");
		}
		Response.append(Buffer.data(), static_cast<std::size_t>(Received));
	}
	closesocket(Socket);
	const std::size_t HeaderEnd = Response.find("\r\n\r\n");
	if (HeaderEnd == std::string::npos)
	{
		throw std::runtime_error("test client response headers missing");
	}
	std::istringstream StatusLine(Response.substr(0, Response.find("\r\n")));
	std::string Version;
	FHttpResult Result;
	StatusLine >> Version >> Result.Status;
	Result.Body = Response.substr(HeaderEnd + 4);
	return Result;
}

void FireAndClose(std::uint16_t Port, const std::string& Body)
{
	FWinsockScope Winsock;
	SOCKET Socket = Connect(Port);
	const std::string Request = BuildRequest(Port, "POST", "/world-adapter/v1/execute", Body, "application/json", "");
	(void)SendAll(Socket, Request);
	shutdown(Socket, SD_BOTH);
	closesocket(Socket);
}

template <typename TCallable>
FHttpResult CallWithPump(FWorldAdapterService& Service, TCallable&& Callable)
{
	auto Future = std::async(std::launch::async, std::forward<TCallable>(Callable));
	const auto Deadline = std::chrono::steady_clock::now() + 10s;
	while (Future.wait_for(0ms) != std::future_status::ready)
	{
		(void)Service.Pump(32);
		if (std::chrono::steady_clock::now() > Deadline)
		{
			throw std::runtime_error("HTTP test request timed out");
		}
		std::this_thread::sleep_for(1ms);
	}
	return Future.get();
}

FWorldAdapterJson ParseBody(const FHttpResult& Result)
{
	FWorldAdapterJson Value;
	FWorldAdapterError Error;
	if (!FWorldAdapterJsonProtocol::ParseJson(Result.Body, Value, Error, Maho::WorldAdapterResponseBodyLimit))
	{
		throw std::runtime_error("HTTP response JSON invalid: " + Error.Message);
	}
	return Value;
}

FWorldAdapterJson SnapshotBody(std::string RequestId)
{
	return {
		{ "adapter_protocol_version", "1.0" },
		{ "request_id", std::move(RequestId) },
		{ "session_id", SessionId },
		{ "world_id", WorldId },
	};
}

FWorldAdapterJson ExecuteBody(
	std::string RequestId,
	std::string ToolCallId,
	std::string ToolName,
	FWorldAdapterJson Args,
	std::uint64_t Revision)
{
	return {
		{ "adapter_protocol_version", "1.0" },
		{ "request_id", std::move(RequestId) },
		{ "session_id", SessionId },
		{ "world_id", WorldId },
		{ "expected_revision", Revision },
		{ "dry_run", false },
		{ "atomic", false },
		{
			"tool_calls",
			FWorldAdapterJson::array({
				{
					{ "tool_call_id", std::move(ToolCallId) },
					{ "tool_name", std::move(ToolName) },
					{ "args", std::move(Args) },
				},
			}),
		},
	};
}

std::unique_ptr<FWorldAdapterService> StartService(
	FWorldAdapterServiceConfig Config = {},
	FStubWorldBackend** OutBackend = nullptr,
	FStubWorldBackendConfig BackendConfig = {})
{
	auto Backend = std::make_unique<FStubWorldBackend>(BackendConfig);
	if (OutBackend)
	{
		*OutBackend = Backend.get();
	}
	auto Service = std::make_unique<FWorldAdapterService>(std::move(Backend));
	Config.Port = Config.Port;
	std::string Error;
	if (!Service->Initialize(Config, Error))
	{
		throw std::runtime_error("service initialize failed: " + Error);
	}
	return Service;
}

} // namespace

void RunHttpTests(FTestRunner& Runner)
{
	Runner.Run("HTTP health and snapshot", [&Runner]()
	{
		FWorldAdapterServiceConfig Config;
		Config.Port = 0;
		auto Service = StartService(Config);
		const FHttpResult Health = HttpRequest(Service->GetPort(), "GET", "/world-adapter/v1/health", {}, {});
		FWorldAdapterError Error;
		Runner.Check(Health.Status == 200 && FWorldAdapterJsonProtocol::ValidateHealthResponse(ParseBody(Health), nullptr, Error), "health endpoint failed");
		const std::string Body = SnapshotBody("11111111-1111-4111-8111-111111111111").dump();
		const FHttpResult Snapshot = CallWithPump(*Service, [&]() { return HttpRequest(Service->GetPort(), "POST", "/world-adapter/v1/snapshot", Body); });
		Runner.Check(Snapshot.Status == 200 && ParseBody(Snapshot).at("world_revision") == 0, "snapshot endpoint failed");
		Service->Shutdown();
	});

	Runner.Run("HTTP summary spawn transform replay", [&Runner]()
	{
		FWorldAdapterServiceConfig Config;
		Config.Port = 0;
		auto Service = StartService(Config);
		auto Execute = [&](const FWorldAdapterJson& Body)
		{
			const std::string Text = Body.dump();
			return CallWithPump(*Service, [&]() { return HttpRequest(Service->GetPort(), "POST", "/world-adapter/v1/execute", Text); });
		};
		const auto Summary = Execute(ExecuteBody("10000000-0000-4000-8000-000000000001", "20000000-0000-4000-8000-000000000001", "world.get_summary", FWorldAdapterJson::object(), 0));
		const FWorldAdapterJson SpawnBody = ExecuteBody("10000000-0000-4000-8000-000000000002", "20000000-0000-4000-8000-000000000002", "entity.spawn_primitive", { { "primitive_type", "cube" }, { "name", "HttpCube" } }, 0);
		const auto Spawn = Execute(SpawnBody);
		const auto Replay = Execute(SpawnBody);
		const std::string EntityId = ParseBody(Spawn).at("tool_results")[0].at("data").at("entity").at("entity_id").get<std::string>();
		const auto Transform = Execute(ExecuteBody("10000000-0000-4000-8000-000000000003", "20000000-0000-4000-8000-000000000003", "entity.set_transform", { { "entity_id", EntityId }, { "transform", { { "position", { 3, 1, 5 } } } } }, 1));
		Runner.Check(Summary.Status == 200 && ParseBody(Summary).at("after_revision") == 0, "summary HTTP result failed");
		Runner.Check(Spawn.Status == 200 && FWorldAdapterJsonProtocol::IsUuid(EntityId), "spawn HTTP result failed");
		Runner.Check(Replay.Status == 200 && ParseBody(Replay).at("replayed") == true, "HTTP replay failed");
		Runner.Check(Transform.Status == 200 && ParseBody(Transform).at("after_revision") == 2, "transform HTTP result failed");
		Service->Shutdown();
	});

	Runner.Run("HTTP undo unsupported", [&Runner]()
	{
		FWorldAdapterServiceConfig Config;
		Config.Port = 0;
		auto Service = StartService(Config);
		const FWorldAdapterJson UndoBody = {
			{ "adapter_protocol_version", "1.0" },
			{ "request_id", "30000000-0000-4000-8000-000000000001" },
			{ "session_id", SessionId },
			{ "world_id", WorldId },
			{ "expected_revision", 0 },
			{ "undo_token", nullptr },
		};
		const std::string Text = UndoBody.dump();
		const FHttpResult Undo = CallWithPump(*Service, [&]() { return HttpRequest(Service->GetPort(), "POST", "/world-adapter/v1/undo", Text); });
		Runner.Check(Undo.Status == 422 && ParseBody(Undo).at("error").at("code") == "UNDO_NOT_AVAILABLE", "undo endpoint did not return unsupported");
		Service->Shutdown();
	});

	Runner.Run("HTTP invalid JSON content type and body limit", [&Runner]()
	{
		FWorldAdapterServiceConfig Config;
		Config.Port = 0;
		auto Service = StartService(Config);
		const auto Invalid = HttpRequest(Service->GetPort(), "POST", "/world-adapter/v1/execute", "{");
		const auto ContentType = HttpRequest(Service->GetPort(), "POST", "/world-adapter/v1/execute", "{}", "text/plain");
		const auto Oversized = HttpRequest(
			Service->GetPort(),
			"POST",
			"/world-adapter/v1/execute",
			{},
			"application/json",
			{},
			Maho::WorldAdapterRequestBodyLimit + 1);
		Runner.Check(Invalid.Status == 400 && ParseBody(Invalid).at("error").at("code") == "INVALID_JSON", "invalid JSON was not rejected");
		Runner.Check(ContentType.Status == 415, "wrong Content-Type was not rejected");
		Runner.Check(Oversized.Status == 413, "oversized body was not rejected");
		Service->Shutdown();
	});

	Runner.Run("HTTP bearer authentication", [&Runner]()
	{
		FWorldAdapterServiceConfig Config;
		Config.Port = 0;
		Config.BearerToken = "test-secret-token";
		auto Service = StartService(Config);
		const auto Missing = HttpRequest(Service->GetPort(), "GET", "/world-adapter/v1/health", {}, {});
		const auto Invalid = HttpRequest(Service->GetPort(), "GET", "/world-adapter/v1/health", {}, {}, "wrong-token");
		const auto Valid = HttpRequest(Service->GetPort(), "GET", "/world-adapter/v1/health", {}, {}, Config.BearerToken);
		Runner.Check(Missing.Status == 401 && Invalid.Status == 401 && Valid.Status == 200, "Bearer authentication result is wrong");
		Runner.Check(Missing.Body.find(Config.BearerToken) == std::string::npos && Invalid.Body.find("wrong-token") == std::string::npos, "token leaked into error body");
		Service->Shutdown();
	});

	Runner.Run("HTTP concurrent requests", [&Runner]()
	{
		FWorldAdapterServiceConfig Config;
		Config.Port = 0;
		Config.HttpWorkerCount = 8;
		auto Service = StartService(Config);
		std::vector<std::future<FHttpResult>> Futures;
		for (int Index = 0; Index < 8; ++Index)
		{
			const std::string Tail = std::to_string(Index + 1);
			const FWorldAdapterJson Body = ExecuteBody(
				"40000000-0000-4000-8000-00000000000" + Tail,
				"50000000-0000-4000-8000-00000000000" + Tail,
				"world.get_summary",
				FWorldAdapterJson::object(),
				0);
			Futures.push_back(std::async(std::launch::async, [Port = Service->GetPort(), Text = Body.dump()]()
			{
				return HttpRequest(Port, "POST", "/world-adapter/v1/execute", Text);
			}));
		}
		bool bReady = false;
		const auto Deadline = std::chrono::steady_clock::now() + 10s;
		while (!bReady && std::chrono::steady_clock::now() < Deadline)
		{
			(void)Service->Pump(32);
			bReady = std::all_of(Futures.begin(), Futures.end(), [](auto& Future) { return Future.wait_for(0ms) == std::future_status::ready; });
			std::this_thread::sleep_for(1ms);
		}
		Runner.Check(bReady, "concurrent HTTP requests did not finish");
		for (auto& Future : Futures)
		{
			Runner.Check(Future.get().Status == 200, "concurrent HTTP request failed");
		}
		Service->Shutdown();
	});

	Runner.Run("HTTP queue backpressure and queued timeout", [&Runner]()
	{
		FWorldAdapterServiceConfig Config;
		Config.Port = 0;
		Config.QueueCapacity = 1;
		Config.RequestTimeout = 80ms;
		Config.HttpWorkerCount = 2;
		FStubWorldBackend* Backend = nullptr;
		auto Service = StartService(Config, &Backend);
		const std::string FirstBody = ExecuteBody("60000000-0000-4000-8000-000000000001", "61000000-0000-4000-8000-000000000001", "entity.spawn_primitive", { { "primitive_type", "cube" } }, 0).dump();
		const std::string SecondBody = ExecuteBody("60000000-0000-4000-8000-000000000002", "61000000-0000-4000-8000-000000000002", "entity.spawn_primitive", { { "primitive_type", "sphere" } }, 0).dump();
		auto First = std::async(std::launch::async, [&]() { return HttpRequest(Service->GetPort(), "POST", "/world-adapter/v1/execute", FirstBody); });
		auto Second = std::async(std::launch::async, [&]() { return HttpRequest(Service->GetPort(), "POST", "/world-adapter/v1/execute", SecondBody); });
		const FHttpResult FirstResult = First.get();
		const FHttpResult SecondResult = Second.get();
		std::array<int, 2> Statuses = { FirstResult.Status, SecondResult.Status };
		std::sort(Statuses.begin(), Statuses.end());
		(void)Service->Pump(4);
		Runner.Check(Statuses == std::array<int, 2>{ 408, 429 }, "queue did not return timeout plus backpressure");
		Runner.Check(Backend->GetRevision(SessionId, WorldId) == 0, "queued timeout executed backend");
		Service->Shutdown();
	});

	Runner.Run("HTTP executing timeout retains replay", [&Runner]()
	{
		FWorldAdapterServiceConfig Config;
		Config.Port = 0;
		Config.RequestTimeout = 100ms;
		FStubWorldBackend* Backend = nullptr;
		auto Service = StartService(Config, &Backend, { 16, 250ms });
		const FWorldAdapterJson Body = ExecuteBody("70000000-0000-4000-8000-000000000001", "71000000-0000-4000-8000-000000000001", "entity.spawn_primitive", { { "primitive_type", "cube" } }, 0);
		const std::string Text = Body.dump();
		const FHttpResult TimedOut = CallWithPump(*Service, [&]() { return HttpRequest(Service->GetPort(), "POST", "/world-adapter/v1/execute", Text); });
		Runner.Check(TimedOut.Status == 408 && Backend->GetRevision(SessionId, WorldId) == 1, "executing timeout lost authoritative write");
		const FHttpResult Replay = CallWithPump(*Service, [&]() { return HttpRequest(Service->GetPort(), "POST", "/world-adapter/v1/execute", Text); });
		Runner.Check(Replay.Status == 200 && ParseBody(Replay).at("replayed") == true, "retry did not replay authoritative result");
		Runner.Check(Backend->GetExecutionCount() == 1 && Backend->GetRevision(SessionId, WorldId) == 1, "timeout retry executed twice");
		Service->Shutdown();
	});

	Runner.Run("HTTP client disconnect keeps committed result", [&Runner]()
	{
		FWorldAdapterServiceConfig Config;
		Config.Port = 0;
		FStubWorldBackend* Backend = nullptr;
		auto Service = StartService(Config, &Backend);
		const std::string Body = ExecuteBody("80000000-0000-4000-8000-000000000001", "81000000-0000-4000-8000-000000000001", "entity.spawn_primitive", { { "primitive_type", "cube" } }, 0).dump();
		std::thread Client([&]() { FireAndClose(Service->GetPort(), Body); });
		Client.join();
		const auto Deadline = std::chrono::steady_clock::now() + 2s;
		while (Backend->GetRevision(SessionId, WorldId) == 0 && std::chrono::steady_clock::now() < Deadline)
		{
			(void)Service->Pump(4);
			std::this_thread::sleep_for(1ms);
		}
		Runner.Check(Backend->GetRevision(SessionId, WorldId) == 1, "client disconnect cancelled committed write");
		Service->Shutdown();
	});

	Runner.Run("HTTP bind rollback and port release", [&Runner]()
	{
		FWorldAdapterServiceConfig FirstConfig;
		FirstConfig.Port = 0;
		auto First = StartService(FirstConfig);
		const std::uint16_t Port = First->GetPort();
		Runner.Check(std::string(First->GetHost()) == "127.0.0.1" && Port != 0, "service is not bound to IPv4 loopback");
		auto SecondBackend = std::make_unique<FStubWorldBackend>();
		FWorldAdapterService Second(std::move(SecondBackend));
		FWorldAdapterServiceConfig SecondConfig;
		SecondConfig.Port = Port;
		std::string Error;
		Runner.Check(!Second.Initialize(SecondConfig, Error), "second service bound an occupied port");
		Second.Shutdown();
		First->Shutdown();
		FWorldAdapterServiceConfig ThirdConfig;
		ThirdConfig.Port = Port;
		auto Third = StartService(ThirdConfig);
		Runner.Check(Third->GetPort() == Port, "shutdown did not release port for immediate rebind");
		Third->Shutdown();
	});
}
