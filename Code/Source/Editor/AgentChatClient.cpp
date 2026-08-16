#include "Editor/AgentChatClient.h"

#include <Core/Misc/Json.h>
#include <Core/Misc/Log.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <Winsock2.h>
#	include <Windows.h>
#	include <iphlpapi.h>
#	include <winhttp.h>
#	pragma comment(lib, "winhttp.lib")
#	pragma comment(lib, "iphlpapi.lib")
#	pragma comment(lib, "ws2_32.lib")
#endif

namespace Maho
{

namespace
{

#if !defined(MAHO_ENGINE_ROOT)
#	define MAHO_ENGINE_ROOT ""
#endif

[[nodiscard]] std::wstring Utf8ToWide(const std::string& Text)
{
#if defined(_WIN32)
	if (Text.empty())
	{
		return std::wstring();
	}
	const int Size = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, nullptr, 0);
	if (Size <= 0)
	{
		return std::wstring();
	}
	std::wstring Out(static_cast<std::size_t>(Size - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, Out.data(), Size);
	return Out;
#else
	(void)Text;
	return {};
#endif
}

[[nodiscard]] EAgentChatRole RoleFromString(const std::string& Role)
{
	if (Role == "user")
	{
		return EAgentChatRole::User;
	}
	if (Role == "assistant")
	{
		return EAgentChatRole::Assistant;
	}
	return EAgentChatRole::System;
}

#if defined(_WIN32)
[[nodiscard]] int TcpPortHostOrder(DWORD NetworkPort)
{
	return static_cast<int>((NetworkPort >> 8) & 0xFF) | static_cast<int>((NetworkPort << 8) & 0xFF00);
}

void ForceFreeListenPort(int Port)
{
	DWORD Size = 0;
	DWORD Result = GetExtendedTcpTable(
		nullptr,
		&Size,
		FALSE,
		AF_INET,
		TCP_TABLE_OWNER_PID_LISTENER,
		0);
	if (Result != ERROR_INSUFFICIENT_BUFFER || Size == 0)
	{
		return;
	}

	std::vector<unsigned char> Buffer(Size);
	auto* Table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(Buffer.data());
	Result = GetExtendedTcpTable(
		Table,
		&Size,
		FALSE,
		AF_INET,
		TCP_TABLE_OWNER_PID_LISTENER,
		0);
	if (Result != NO_ERROR)
	{
		return;
	}

	for (DWORD Index = 0; Index < Table->dwNumEntries; ++Index)
	{
		const MIB_TCPROW_OWNER_PID& Row = Table->table[Index];
		if (TcpPortHostOrder(Row.dwLocalPort) != Port || Row.dwOwningPid == 0)
		{
			continue;
		}
		HANDLE Process = OpenProcess(PROCESS_TERMINATE, FALSE, Row.dwOwningPid);
		if (Process)
		{
			TerminateProcess(Process, 0);
			CloseHandle(Process);
		}
	}
}
#endif

} // namespace

struct FAgentChatClient::FImpl
{
	FAgentChatStartOptions Options;
	std::atomic<bool> bRunning{false};
	std::atomic<bool> bConnected{false};
	std::atomic<bool> bBusy{false};
	std::atomic<bool> bMock{false};
	std::string StatusText = "Idle";
	mutable std::mutex StatusMutex;

	std::thread Worker;
	std::mutex SendMutex;
	std::condition_variable SendCv;
	std::queue<std::string> SendQueue;

	std::mutex InboxMutex;
	std::vector<FAgentChatBubble> Inbox;

	std::int64_t LastEventId = -1;
	std::string ApiKeyFilePath;

#if defined(_WIN32)
	HANDLE BridgeProcess = nullptr;
#endif

	void SetStatus(std::string Text)
	{
		std::lock_guard<std::mutex> Lock(StatusMutex);
		StatusText = std::move(Text);
	}

	[[nodiscard]] std::string GetStatusCopy() const
	{
		std::lock_guard<std::mutex> Lock(StatusMutex);
		return StatusText;
	}

	void PushInbox(EAgentChatRole Role, std::string Text)
	{
		FAgentChatBubble Bubble;
		Bubble.Role = Role;
		Bubble.Text = std::move(Text);
		std::lock_guard<std::mutex> Lock(InboxMutex);
		Inbox.push_back(std::move(Bubble));
	}

#if defined(_WIN32)
	[[nodiscard]] bool HttpRequest(
		const wchar_t* Method,
		const std::wstring& Path,
		const std::string& Body,
		std::string& OutBody,
		unsigned long& OutStatus) const
	{
		OutBody.clear();
		OutStatus = 0;

		HINTERNET Session = WinHttpOpen(
			L"MahoAgentChat/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (!Session)
		{
			return false;
		}

		HINTERNET Connect = WinHttpConnect(Session, L"127.0.0.1", static_cast<INTERNET_PORT>(Options.Port), 0);
		if (!Connect)
		{
			WinHttpCloseHandle(Session);
			return false;
		}

		DWORD Flags = 0;
		HINTERNET Request = WinHttpOpenRequest(
			Connect,
			Method,
			Path.c_str(),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			Flags);
		if (!Request)
		{
			WinHttpCloseHandle(Connect);
			WinHttpCloseHandle(Session);
			return false;
		}

		std::wstring Headers = L"Content-Type: application/json\r\n";
		BOOL Ok = WinHttpSendRequest(
			Request,
			Headers.c_str(),
			static_cast<DWORD>(-1),
			Body.empty() ? WINHTTP_NO_REQUEST_DATA : reinterpret_cast<LPVOID>(const_cast<char*>(Body.data())),
			static_cast<DWORD>(Body.size()),
			static_cast<DWORD>(Body.size()),
			0);

		if (Ok)
		{
			Ok = WinHttpReceiveResponse(Request, nullptr);
		}

		if (Ok)
		{
			DWORD StatusCode = 0;
			DWORD StatusSize = sizeof(StatusCode);
			WinHttpQueryHeaders(
				Request,
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX,
				&StatusCode,
				&StatusSize,
				WINHTTP_NO_HEADER_INDEX);
			OutStatus = StatusCode;

			for (;;)
			{
				DWORD Available = 0;
				if (!WinHttpQueryDataAvailable(Request, &Available))
				{
					Ok = FALSE;
					break;
				}
				if (Available == 0)
				{
					break;
				}
				std::string Chunk(Available, '\0');
				DWORD Read = 0;
				if (!WinHttpReadData(Request, Chunk.data(), Available, &Read))
				{
					Ok = FALSE;
					break;
				}
				Chunk.resize(Read);
				OutBody.append(Chunk);
			}
		}

		WinHttpCloseHandle(Request);
		WinHttpCloseHandle(Connect);
		WinHttpCloseHandle(Session);
		return Ok == TRUE;
	}

	[[nodiscard]] bool TryHealth()
	{
		std::string Body;
		unsigned long Status = 0;
		if (!HttpRequest(L"GET", L"/health", {}, Body, Status) || Status != 200)
		{
			bConnected = false;
			return false;
		}

		FJsonDocument Doc;
		if (!Doc.Parse(Body))
		{
			bConnected = false;
			return false;
		}

		const FJsonValue& Root = Doc.GetRoot();
		bConnected = Root.GetField("ok").AsBool(false);
		bMock = Root.GetField("mock").AsBool(false);
		bBusy = Root.GetField("busy").AsBool(false);
		const std::string BridgeStatus = Root.GetField("status").AsString("connected");
		SetStatus(BridgeStatus);
		return bConnected.load();
	}

	void PollEvents()
	{
		const std::wstring Path = L"/events?after=" + std::to_wstring(LastEventId);
		std::string Body;
		unsigned long Status = 0;
		if (!HttpRequest(L"GET", Path, {}, Body, Status) || Status != 200)
		{
			bConnected = false;
			return;
		}

		FJsonDocument Doc;
		if (!Doc.Parse(Body))
		{
			return;
		}

		const FJsonValue& Root = Doc.GetRoot();
		bBusy = Root.GetField("busy").AsBool(false);
		bMock = Root.GetField("mock").AsBool(false);
		bConnected = true;
		const std::string BridgeStatus = Root.GetField("status").AsString("connected");
		SetStatus(BridgeStatus);

		const FJsonValue Events = Root.GetField("events");
		const std::size_t Count = Events.GetArraySize();
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			const FJsonValue Event = Events.GetElement(Index);
			const std::int64_t Id = Event.GetField("id").AsInt64(-1);
			if (Id > LastEventId)
			{
				LastEventId = Id;
			}
			const std::string Role = Event.GetField("role").AsString("system");
			const std::string Text = Event.GetField("text").AsString("");
			if (!Text.empty())
			{
				PushInbox(RoleFromString(Role), Text);
			}
		}
	}

	void PostChat(const std::string& Message)
	{
		FJsonValue Root = FJsonValue::Object();
		Root.SetField("message", FJsonValue::String(Message));
		FJsonDocument Doc;
		Doc.SetRoot(Root);
		const std::string Body = Doc.Stringify(false);

		std::string Response;
		unsigned long Status = 0;
		if (!HttpRequest(L"POST", L"/chat", Body, Response, Status))
		{
			PushInbox(EAgentChatRole::System, "Failed to reach Agent bridge (POST /chat).");
			bConnected = false;
			return;
		}
		if (Status != 200)
		{
			std::string Detail = Response;
			FJsonDocument ErrDoc;
			if (ErrDoc.Parse(Response))
			{
				Detail = ErrDoc.GetRoot().GetField("error").AsString(Response.c_str());
			}
			PushInbox(EAgentChatRole::System, "Agent bridge error: " + Detail);
			return;
		}
		bBusy = true;
		SetStatus("Thinking...");
	}

	[[nodiscard]] bool WriteApiKeyFile()
	{
		namespace fs = std::filesystem;
		if (Options.ApiKey.empty())
		{
			ApiKeyFilePath.clear();
			return true;
		}

		const fs::path KeyPath =
			fs::path(Options.ProjectCwd) / "Saved" / "Agent" / ".cursor_api_key";
		std::error_code ErrorCode;
		fs::create_directories(KeyPath.parent_path(), ErrorCode);
		std::ofstream Out(KeyPath, std::ios::binary | std::ios::trunc);
		if (!Out)
		{
			PushInbox(EAgentChatRole::System, "Failed to write Saved/Agent/.cursor_api_key");
			return false;
		}
		Out.write(Options.ApiKey.data(), static_cast<std::streamsize>(Options.ApiKey.size()));
		Out.close();
		ApiKeyFilePath = KeyPath.string();
		return true;
	}

	[[nodiscard]] bool SpawnBridgeProcess()
	{
		namespace fs = std::filesystem;
		const fs::path BridgeDir(Options.BridgeDirectory);
		const fs::path Script = BridgeDir / "server.mjs";
		if (!fs::exists(Script))
		{
			SetStatus("Bridge script missing: " + Script.string());
			PushInbox(
				EAgentChatRole::System,
				"Agent bridge not found at:\n" + Script.string()
					+ "\nExpected engine Tools/AgentBridge/server.mjs");
			return false;
		}

		if (!WriteApiKeyFile())
		{
			return false;
		}

		std::wstring Command = L"node \"";
		Command += Utf8ToWide(Script.string());
		Command += L"\" --port ";
		Command += std::to_wstring(Options.Port);
		Command += L" --cwd \"";
		Command += Utf8ToWide(Options.ProjectCwd);
		Command += L"\"";
		if (!ApiKeyFilePath.empty())
		{
			Command += L" --api-key-file \"";
			Command += Utf8ToWide(ApiKeyFilePath);
			Command += L"\"";
		}

		STARTUPINFOW Startup{};
		Startup.cb = sizeof(Startup);
		PROCESS_INFORMATION Info{};

		std::wstring EnvBlock = BuildChildEnvironment(Options.ApiKey);
		std::wstring CommandMutable = Command;
		const DWORD CreationFlags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
		const BOOL Ok = CreateProcessW(
			nullptr,
			CommandMutable.data(),
			nullptr,
			nullptr,
			FALSE,
			CreationFlags,
			EnvBlock.empty() ? nullptr : EnvBlock.data(),
			Utf8ToWide(BridgeDir.string()).c_str(),
			&Startup,
			&Info);

		if (!Ok)
		{
			SetStatus("Failed to spawn node bridge");
			PushInbox(
				EAgentChatRole::System,
				"Could not start `node server.mjs`. Is Node.js on PATH? "
				"Also run `npm install` in Tools/AgentBridge.");
			return false;
		}

		CloseHandle(Info.hThread);
		BridgeProcess = Info.hProcess;
		SetStatus("Starting bridge...");
		PushInbox(
			EAgentChatRole::System,
			std::string("Spawned Agent bridge (ApiKey ")
				+ (Options.ApiKey.empty() ? "missing" : "len=" + std::to_string(Options.ApiKey.size()))
				+ ").");
		return true;
	}

	[[nodiscard]] static std::wstring BuildChildEnvironment(const std::string& ApiKey)
	{
		LPWCH Parent = GetEnvironmentStringsW();
		if (!Parent)
		{
			return {};
		}

		std::wstring Block;
		for (LPWCH Cursor = Parent; *Cursor != L'\0'; )
		{
			const std::wstring Entry = Cursor;
			Cursor += Entry.size() + 1;
			if (Entry.rfind(L"CURSOR_API_KEY=", 0) == 0
				|| Entry.rfind(L"MAHO_AGENT_MOCK=", 0) == 0)
			{
				continue;
			}
			Block.append(Entry);
			Block.push_back(L'\0');
		}
		FreeEnvironmentStringsW(Parent);

		if (!ApiKey.empty())
		{
			Block.append(L"CURSOR_API_KEY=");
			Block.append(Utf8ToWide(ApiKey));
			Block.push_back(L'\0');
		}
		Block.push_back(L'\0');
		return Block;
	}

	void RequestBridgeShutdown()
	{
		std::string Body;
		unsigned long Status = 0;
		(void)HttpRequest(L"POST", L"/shutdown", "{}", Body, Status);
		KillBridgeProcess();
		ForceFreeListenPort(Options.Port);
		std::this_thread::sleep_for(std::chrono::milliseconds(400));
	}

	void KillBridgeProcess()
	{
		if (BridgeProcess)
		{
			TerminateProcess(BridgeProcess, 0);
			CloseHandle(BridgeProcess);
			BridgeProcess = nullptr;
		}
	}
#endif

	void WorkerMain()
	{
#if !defined(_WIN32)
		SetStatus("Agent chat is Windows-only for now");
		PushInbox(EAgentChatRole::System, "Agent chat bridge is only implemented on Windows.");
		return;
#else
		const bool bHaveKey = !Options.ApiKey.empty();
		if (bHaveKey)
		{
			SetStatus("Connecting with ApiKey from DefaultEditor.ini...");
			PushInbox(
				EAgentChatRole::System,
				"Loaded ApiKey from DefaultEditor.ini (len="
					+ std::to_string(Options.ApiKey.size()) + "). Restarting bridge...");
		}
		else
		{
			PushInbox(
				EAgentChatRole::System,
				"No ApiKey loaded — bridge will stay in mock mode unless CURSOR_API_KEY is set.");
		}

		if (Options.bSpawnBridge)
		{
			// Always tear down whatever is on the port so a stale mock bridge cannot stick.
			RequestBridgeShutdown();
			(void)SpawnBridgeProcess();
		}

		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
		while (bRunning.load() && !TryHealth())
		{
			if (std::chrono::steady_clock::now() > Deadline)
			{
				SetStatus("Bridge timeout");
				PushInbox(
					EAgentChatRole::System,
					"Timed out waiting for Agent bridge on 127.0.0.1:"
						+ std::to_string(Options.Port)
						+ ". Check Tools/AgentBridge (`npm install`, Node 22+) and DefaultEditor.ini.");
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}

		if (bConnected.load())
		{
			if (bMock.load())
			{
				PushInbox(
					EAgentChatRole::System,
					"Bridge is still in mock mode after restart. "
					"Confirm Config/DefaultEditor.ini [Agent] ApiKey and Node >= 22.13.");
			}
			else
			{
				PushInbox(EAgentChatRole::System, "Cursor Agent connected (not mock).");
			}
		}

		while (bRunning.load())
		{
			std::string Pending;
			{
				std::unique_lock<std::mutex> Lock(SendMutex);
				SendCv.wait_for(Lock, std::chrono::milliseconds(250), [this]()
				{
					return !bRunning.load() || !SendQueue.empty();
				});
				if (!bRunning.load())
				{
					break;
				}
				if (!SendQueue.empty())
				{
					Pending = std::move(SendQueue.front());
					SendQueue.pop();
				}
			}

			if (!Pending.empty())
			{
				PostChat(Pending);
			}

			if (bConnected.load() || TryHealth())
			{
				PollEvents();
			}
		}

		RequestBridgeShutdown();
#endif
	}
};

FAgentChatClient::FAgentChatClient()
	: Impl(std::make_unique<FImpl>())
{
}

FAgentChatClient::~FAgentChatClient()
{
	Stop();
}

void FAgentChatClient::Start(const FAgentChatStartOptions& Options)
{
	Stop();
	Impl->Options = Options;
	if (Impl->Options.BridgeDirectory.empty())
	{
		Impl->Options.BridgeDirectory = std::string(MAHO_ENGINE_ROOT) + "/Tools/AgentBridge";
	}
	if (Impl->Options.ProjectCwd.empty())
	{
		Impl->Options.ProjectCwd = std::filesystem::current_path().string();
	}
	Impl->LastEventId = -1;
	Impl->bRunning = true;
	Impl->SetStatus("Connecting...");
	Impl->Worker = std::thread([this]()
	{
		Impl->WorkerMain();
	});
}

void FAgentChatClient::Stop()
{
	if (!Impl)
	{
		return;
	}
	Impl->bRunning = false;
	Impl->SendCv.notify_all();
	if (Impl->Worker.joinable())
	{
		Impl->Worker.join();
	}
	Impl->bConnected = false;
	Impl->bBusy = false;
	Impl->SetStatus("Stopped");
}

void FAgentChatClient::Tick()
{
}

void FAgentChatClient::SendUserMessage(std::string Text)
{
	if (Text.empty() || !Impl->bRunning.load())
	{
		return;
	}
	{
		std::lock_guard<std::mutex> Lock(Impl->SendMutex);
		Impl->SendQueue.push(std::move(Text));
	}
	Impl->SendCv.notify_one();
}

void FAgentChatClient::DrainRemoteBubbles(std::vector<FAgentChatBubble>& Out)
{
	Out.clear();
	std::lock_guard<std::mutex> Lock(Impl->InboxMutex);
	Out.swap(Impl->Inbox);
}

bool FAgentChatClient::IsStarted() const
{
	return Impl && Impl->bRunning.load();
}

bool FAgentChatClient::IsConnected() const
{
	return Impl && Impl->bConnected.load();
}

bool FAgentChatClient::IsBusy() const
{
	return Impl && Impl->bBusy.load();
}

bool FAgentChatClient::IsMockMode() const
{
	return Impl && Impl->bMock.load();
}

std::string FAgentChatClient::GetStatusText() const
{
	return Impl ? Impl->GetStatusCopy() : std::string("Idle");
}

} // namespace Maho
