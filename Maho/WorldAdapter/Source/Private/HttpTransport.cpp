#include <WorldAdapter/Transport/HttpTransport.h>

#if defined(_WIN32)
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <WinSock2.h>
#	include <WS2tcpip.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace Maho
{

namespace
{

#if defined(_WIN32)
using FSocket = SOCKET;
constexpr FSocket InvalidSocket = INVALID_SOCKET;
#else
using FSocket = int;
constexpr FSocket InvalidSocket = -1;
#endif

constexpr std::size_t HeaderLimit = 64U * 1024U;

void CloseSocket(FSocket& Socket) noexcept
{
#if defined(_WIN32)
	if (Socket != InvalidSocket)
	{
		shutdown(Socket, SD_BOTH);
		closesocket(Socket);
		Socket = InvalidSocket;
	}
#else
	(void)Socket;
#endif
}

std::string Lowercase(std::string Value)
{
	std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Character)
	{
		return static_cast<char>(std::tolower(Character));
	});
	return Value;
}

std::string Trim(std::string Value)
{
	while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.front())))
	{
		Value.erase(Value.begin());
	}
	while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.back())))
	{
		Value.pop_back();
	}
	return Value;
}

const char* StatusText(int Status)
{
	switch (Status)
	{
	case 200: return "OK";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 404: return "Not Found";
	case 408: return "Request Timeout";
	case 409: return "Conflict";
	case 413: return "Payload Too Large";
	case 415: return "Unsupported Media Type";
	case 422: return "Unprocessable Content";
	case 429: return "Too Many Requests";
	case 500: return "Internal Server Error";
	case 503: return "Service Unavailable";
	default: return "Error";
	}
}

FWorldAdapterHttpResponse SafeError(int Status, const char* Code, const char* Message)
{
	FWorldAdapterJson Body = {
		{ "ok", false },
		{ "adapter_protocol_version", WorldAdapterProtocolVersion },
		{
			"error",
			{
				{ "code", Code },
				{ "message", Message },
				{ "details", FWorldAdapterJson::object() },
				{ "retryable", Status == 408 || Status == 429 || Status >= 500 },
			},
		},
	};
	return { Status, Body.dump() };
}

bool SendAll(FSocket Socket, const std::string& Data) noexcept
{
#if defined(_WIN32)
	std::size_t Offset = 0;
	while (Offset < Data.size())
	{
		const int Chunk = static_cast<int>((std::min)(Data.size() - Offset, static_cast<std::size_t>((std::numeric_limits<int>::max)())));
		const int Sent = send(Socket, Data.data() + Offset, Chunk, 0);
		if (Sent <= 0)
		{
			return false;
		}
		Offset += static_cast<std::size_t>(Sent);
	}
	return true;
#else
	(void)Socket;
	(void)Data;
	return false;
#endif
}

void SendResponse(FSocket Socket, FWorldAdapterHttpResponse Response, std::size_t ResponseLimit) noexcept
{
	try
	{
		if (Response.Body.size() > ResponseLimit)
		{
			Response = SafeError(500, "RESPONSE_TOO_LARGE", "World adapter response exceeded the configured limit");
		}
		std::ostringstream Stream;
		Stream << "HTTP/1.1 " << Response.Status << ' ' << StatusText(Response.Status) << "\r\n"
			<< "Content-Type: application/json; charset=utf-8\r\n"
			<< "Content-Length: " << Response.Body.size() << "\r\n"
			<< "Connection: close\r\n"
			<< "Cache-Control: no-store\r\n"
			<< "X-Content-Type-Options: nosniff\r\n\r\n"
			<< Response.Body;
		(void)SendAll(Socket, Stream.str());
	}
	catch (...)
	{
	}
}

bool ParseUnsignedSize(const std::string& Text, std::size_t& OutValue)
{
	if (Text.empty())
	{
		return false;
	}
	std::size_t Value = 0;
	for (char Character : Text)
	{
		if (Character < '0' || Character > '9')
		{
			return false;
		}
		const std::size_t Digit = static_cast<std::size_t>(Character - '0');
		if (Value > ((std::numeric_limits<std::size_t>::max)() - Digit) / 10)
		{
			return false;
		}
		Value = Value * 10 + Digit;
	}
	OutValue = Value;
	return true;
}

bool ReadRequest(
	FSocket Socket,
	std::size_t BodyLimit,
	FWorldAdapterHttpRequest& OutRequest,
	FWorldAdapterHttpResponse& OutFailure) noexcept
{
#if defined(_WIN32)
	try
	{
		std::string Buffer;
		Buffer.reserve(4096);
		std::array<char, 4096> Chunk{};
		std::size_t HeaderEnd = std::string::npos;
		while ((HeaderEnd = Buffer.find("\r\n\r\n")) == std::string::npos)
		{
			if (Buffer.size() >= HeaderLimit)
			{
				OutFailure = SafeError(400, "INVALID_REQUEST", "HTTP headers exceed the configured limit");
				return false;
			}
			const int Received = recv(Socket, Chunk.data(), static_cast<int>(Chunk.size()), 0);
			if (Received <= 0)
			{
				return false;
			}
			Buffer.append(Chunk.data(), static_cast<std::size_t>(Received));
		}

		const std::string HeadersText = Buffer.substr(0, HeaderEnd);
		std::istringstream HeaderStream(HeadersText);
		std::string RequestLine;
		if (!std::getline(HeaderStream, RequestLine))
		{
			OutFailure = SafeError(400, "INVALID_REQUEST", "HTTP request line is missing");
			return false;
		}
		if (!RequestLine.empty() && RequestLine.back() == '\r')
		{
			RequestLine.pop_back();
		}
		std::istringstream RequestLineStream(RequestLine);
		std::string Version;
		std::string Extra;
		if (!(RequestLineStream >> OutRequest.Method >> OutRequest.Path >> Version) ||
			(RequestLineStream >> Extra) ||
			(Version != "HTTP/1.1" && Version != "HTTP/1.0"))
		{
			OutFailure = SafeError(400, "INVALID_REQUEST", "HTTP request line is invalid");
			return false;
		}

		std::string Line;
		while (std::getline(HeaderStream, Line))
		{
			if (!Line.empty() && Line.back() == '\r')
			{
				Line.pop_back();
			}
			if (Line.empty() || Line.front() == ' ' || Line.front() == '\t')
			{
				OutFailure = SafeError(400, "INVALID_REQUEST", "HTTP header syntax is invalid");
				return false;
			}
			const std::size_t Separator = Line.find(':');
			if (Separator == std::string::npos)
			{
				OutFailure = SafeError(400, "INVALID_REQUEST", "HTTP header syntax is invalid");
				return false;
			}
			const std::string Name = Lowercase(Trim(Line.substr(0, Separator)));
			const std::string Value = Trim(Line.substr(Separator + 1));
			if (Name.empty() || OutRequest.Headers.contains(Name))
			{
				OutFailure = SafeError(400, "INVALID_REQUEST", "Duplicate or empty HTTP header is not allowed");
				return false;
			}
			OutRequest.Headers.emplace(Name, Value);
		}

		if (OutRequest.Headers.contains("transfer-encoding"))
		{
			OutFailure = SafeError(400, "INVALID_REQUEST", "Transfer-Encoding is not supported");
			return false;
		}
		std::size_t ContentLength = 0;
		if (OutRequest.Headers.contains("content-length") &&
			!ParseUnsignedSize(OutRequest.Headers.at("content-length"), ContentLength))
		{
			OutFailure = SafeError(400, "INVALID_REQUEST", "Content-Length is invalid");
			return false;
		}
		if (ContentLength > BodyLimit)
		{
			OutFailure = SafeError(413, "REQUEST_TOO_LARGE", "Request body exceeds the configured size limit");
			return false;
		}
		const std::size_t BodyStart = HeaderEnd + 4;
		while (Buffer.size() - BodyStart < ContentLength)
		{
			const int Received = recv(Socket, Chunk.data(), static_cast<int>(Chunk.size()), 0);
			if (Received <= 0)
			{
				return false;
			}
			Buffer.append(Chunk.data(), static_cast<std::size_t>(Received));
		}
		OutRequest.Body = Buffer.substr(BodyStart, ContentLength);
		return true;
	}
	catch (...)
	{
		OutFailure = SafeError(400, "INVALID_REQUEST", "HTTP request failed safe parsing");
		return false;
	}
#else
	(void)Socket;
	(void)BodyLimit;
	(void)OutRequest;
	OutFailure = SafeError(503, "ADAPTER_UNAVAILABLE", "HTTP transport is unavailable on this platform");
	return false;
#endif
}

} // namespace

struct FWorldAdapterHttpTransport::FImpl
{
	FWorldAdapterHttpTransportConfig Config;
	FRequestHandler Handler;
	FSocket ListenSocket = InvalidSocket;
	std::thread AcceptThread;
	std::vector<std::thread> Workers;
	std::deque<FSocket> PendingSockets;
	std::mutex Mutex;
	std::condition_variable Condition;
	std::atomic<bool> bRunning = false;
	std::atomic<bool> bStopping = false;
	std::uint16_t BoundPort = 0;
	bool bWinsockInitialized = false;

	void HandleSocket(FSocket Socket) noexcept
	{
		FWorldAdapterHttpRequest Request;
		FWorldAdapterHttpResponse Failure;
		if (!ReadRequest(Socket, Config.RequestBodyLimit, Request, Failure))
		{
			if (!Failure.Body.empty())
			{
				SendResponse(Socket, std::move(Failure), Config.ResponseBodyLimit);
			}
			CloseSocket(Socket);
			return;
		}
		FWorldAdapterHttpResponse Response;
		try
		{
			Response = Handler ? Handler(Request) : SafeError(503, "ADAPTER_UNAVAILABLE", "World adapter handler is unavailable");
		}
		catch (...)
		{
			Response = SafeError(500, "INTERNAL_ERROR", "World adapter request failed safely");
		}
		SendResponse(Socket, std::move(Response), Config.ResponseBodyLimit);
		CloseSocket(Socket);
	}

	void WorkerMain() noexcept
	{
		for (;;)
		{
			FSocket Socket = InvalidSocket;
			{
				std::unique_lock<std::mutex> Lock(Mutex);
				Condition.wait(Lock, [this]()
				{
					return bStopping.load() || !PendingSockets.empty();
				});
				if (PendingSockets.empty())
				{
					if (bStopping.load())
					{
						return;
					}
					continue;
				}
				Socket = PendingSockets.front();
				PendingSockets.pop_front();
			}
			HandleSocket(Socket);
		}
	}

	void AcceptMain() noexcept
	{
#if defined(_WIN32)
		while (!bStopping.load())
		{
			FSocket Socket = accept(ListenSocket, nullptr, nullptr);
			if (Socket == InvalidSocket)
			{
				if (bStopping.load())
				{
					break;
				}
				continue;
			}
			const DWORD TimeoutMs = 5000;
			setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&TimeoutMs), sizeof(TimeoutMs));
			setsockopt(Socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&TimeoutMs), sizeof(TimeoutMs));
			bool bQueued = false;
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (!bStopping.load() && PendingSockets.size() < Config.ConnectionQueueCapacity)
				{
					PendingSockets.push_back(Socket);
					bQueued = true;
				}
			}
			if (bQueued)
			{
				Condition.notify_one();
			}
			else
			{
				SendResponse(Socket, SafeError(503, "BUSY", "HTTP connection queue is full"), Config.ResponseBodyLimit);
				CloseSocket(Socket);
			}
		}
#endif
	}
};

FWorldAdapterHttpTransport::FWorldAdapterHttpTransport()
	: Impl(std::make_unique<FImpl>())
{
}

FWorldAdapterHttpTransport::~FWorldAdapterHttpTransport()
{
	Shutdown();
}

bool FWorldAdapterHttpTransport::Initialize(
	const FWorldAdapterHttpTransportConfig& Config,
	FRequestHandler Handler,
	std::string& OutError) noexcept
{
#if defined(_WIN32)
	try
	{
		if (!Impl || Impl->bRunning.load() || !Handler || Config.WorkerCount == 0 || Config.ConnectionQueueCapacity == 0)
		{
			OutError = "HTTP transport configuration is invalid";
			return false;
		}
		Impl->Config = Config;
		Impl->Handler = std::move(Handler);
		WSADATA WinsockData{};
		if (WSAStartup(MAKEWORD(2, 2), &WinsockData) != 0)
		{
			OutError = "WSAStartup failed";
			return false;
		}
		Impl->bWinsockInitialized = true;
		Impl->ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (Impl->ListenSocket == InvalidSocket)
		{
			OutError = "Unable to create loopback listen socket";
			Shutdown();
			return false;
		}
		const BOOL Exclusive = TRUE;
		if (setsockopt(Impl->ListenSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&Exclusive), sizeof(Exclusive)) == SOCKET_ERROR)
		{
			OutError = "Unable to make loopback listen socket exclusive";
			Shutdown();
			return false;
		}
		sockaddr_in Address{};
		Address.sin_family = AF_INET;
		Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		Address.sin_port = htons(Config.Port);
		if (bind(Impl->ListenSocket, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) == SOCKET_ERROR ||
			listen(Impl->ListenSocket, SOMAXCONN) == SOCKET_ERROR)
		{
			OutError = "Unable to bind 127.0.0.1:" + std::to_string(Config.Port);
			Shutdown();
			return false;
		}
		int AddressSize = sizeof(Address);
		if (getsockname(Impl->ListenSocket, reinterpret_cast<sockaddr*>(&Address), &AddressSize) == SOCKET_ERROR)
		{
			OutError = "Unable to query bound loopback port";
			Shutdown();
			return false;
		}
		Impl->BoundPort = ntohs(Address.sin_port);
		Impl->bStopping.store(false);
		Impl->bRunning.store(true);
		Impl->Workers.reserve(Config.WorkerCount);
		for (std::size_t Index = 0; Index < Config.WorkerCount; ++Index)
		{
			Impl->Workers.emplace_back([this]() { Impl->WorkerMain(); });
		}
		Impl->AcceptThread = std::thread([this]() { Impl->AcceptMain(); });
		return true;
	}
	catch (...)
	{
		OutError = "HTTP transport initialization failed safely";
		Shutdown();
		return false;
	}
#else
	(void)Config;
	(void)Handler;
	OutError = "World Adapter HTTP transport currently supports Windows only";
	return false;
#endif
}

void FWorldAdapterHttpTransport::BeginShutdown() noexcept
{
	if (!Impl || Impl->bStopping.exchange(true))
	{
		return;
	}
	Impl->bRunning.store(false);
	CloseSocket(Impl->ListenSocket);
	std::deque<FSocket> Pending;
	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		Pending.swap(Impl->PendingSockets);
	}
	for (FSocket& Socket : Pending)
	{
		CloseSocket(Socket);
	}
	Impl->Condition.notify_all();
}

void FWorldAdapterHttpTransport::Shutdown() noexcept
{
	if (!Impl)
	{
		return;
	}
	BeginShutdown();
	if (Impl->AcceptThread.joinable())
	{
		Impl->AcceptThread.join();
	}
	for (std::thread& Worker : Impl->Workers)
	{
		if (Worker.joinable())
		{
			Worker.join();
		}
	}
	Impl->Workers.clear();
#if defined(_WIN32)
	if (Impl->bWinsockInitialized)
	{
		WSACleanup();
		Impl->bWinsockInitialized = false;
	}
#endif
	Impl->Handler = {};
	Impl->BoundPort = 0;
}

bool FWorldAdapterHttpTransport::IsRunning() const noexcept
{
	return Impl && Impl->bRunning.load();
}

std::uint16_t FWorldAdapterHttpTransport::GetPort() const noexcept
{
	return Impl ? Impl->BoundPort : 0;
}

} // namespace Maho
