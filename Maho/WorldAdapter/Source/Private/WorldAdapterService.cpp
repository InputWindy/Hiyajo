#include <WorldAdapter/Service/WorldAdapterService.h>

#include <WorldAdapter/Protocol/Json.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace Maho
{

namespace
{

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

bool IsJsonContentType(const std::unordered_map<std::string, std::string>& Headers)
{
	const auto It = Headers.find("content-type");
	if (It == Headers.end())
	{
		return false;
	}
	std::string Value = Lowercase(Trim(It->second));
	if (Value == "application/json")
	{
		return true;
	}
	const std::size_t Separator = Value.find(';');
	if (Separator == std::string::npos || Trim(Value.substr(0, Separator)) != "application/json")
	{
		return false;
	}
	return Trim(Value.substr(Separator + 1)) == "charset=utf-8";
}

bool ConstantTimeEqual(const std::string& Left, const std::string& Right) noexcept
{
	const std::size_t Maximum = (std::max)(Left.size(), Right.size());
	std::size_t Difference = Left.size() ^ Right.size();
	for (std::size_t Index = 0; Index < Maximum; ++Index)
	{
		const unsigned char LeftByte = Index < Left.size() ? static_cast<unsigned char>(Left[Index]) : 0U;
		const unsigned char RightByte = Index < Right.size() ? static_cast<unsigned char>(Right[Index]) : 0U;
		Difference |= static_cast<std::size_t>(LeftByte ^ RightByte);
	}
	return Difference == 0;
}

FWorldAdapterHttpResponse JsonResponse(int Status, const FWorldAdapterJson& Body)
{
	return { Status, FWorldAdapterJsonProtocol::Serialize(Body) };
}

FWorldAdapterHttpResponse ErrorResponse(int Status, FWorldAdapterError Error)
{
	return JsonResponse(Status, {
		{ "ok", false },
		{ "adapter_protocol_version", WorldAdapterProtocolVersion },
		{ "error", FWorldAdapterJsonProtocol::ToJson(Error) },
	});
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

int ErrorStatus(const std::optional<FWorldAdapterError>& Error)
{
	if (!Error)
	{
		return 200;
	}
	if (Error->Code == "REVISION_CONFLICT" || Error->Code == "REQUEST_ID_CONFLICT")
	{
		return 409;
	}
	if (Error->Code == "ENTITY_NOT_FOUND")
	{
		return 404;
	}
	if (Error->Code == "TIMEOUT")
	{
		return 408;
	}
	if (Error->Code == "BUSY")
	{
		return 429;
	}
	if (Error->Code == "ADAPTER_UNAVAILABLE" || Error->Code == "INTERNAL_ERROR")
	{
		return 503;
	}
	return 422;
}

} // namespace

FWorldAdapterService::FWorldAdapterService(std::unique_ptr<IWorldAdapterBackend> InBackend)
	: Backend(std::move(InBackend))
{
}

FWorldAdapterService::~FWorldAdapterService()
{
	Shutdown();
}

bool FWorldAdapterService::Initialize(const FWorldAdapterServiceConfig& InConfig, std::string& OutError) noexcept
{
	try
	{
		if (bInitialized.load() || bShuttingDown.load() || !Backend)
		{
			OutError = "World Adapter Service is already initialized or has no Backend";
			return false;
		}
		if (InConfig.QueueCapacity == 0 ||
			InConfig.RequestTimeout.count() <= 0 ||
			InConfig.RequestBodyLimit == 0 ||
			InConfig.RequestBodyLimit > WorldAdapterRequestBodyLimit ||
			InConfig.ResponseBodyLimit == 0 ||
			InConfig.ResponseBodyLimit > WorldAdapterResponseBodyLimit ||
			InConfig.HttpWorkerCount == 0 ||
			InConfig.HttpConnectionQueueCapacity == 0 ||
			InConfig.BearerToken.size() > 4096)
		{
			OutError = "World Adapter Service configuration is invalid";
			return false;
		}

		Config = InConfig;
		Capabilities = Backend->GetCapabilities();
		Queue = std::make_unique<FWorldAdapterCommandQueue>(Config.QueueCapacity);
		Transport = std::make_unique<FWorldAdapterHttpTransport>();
		FWorldAdapterHttpTransportConfig TransportConfig;
		TransportConfig.Port = Config.Port;
		TransportConfig.RequestBodyLimit = Config.RequestBodyLimit;
		TransportConfig.ResponseBodyLimit = Config.ResponseBodyLimit;
		TransportConfig.WorkerCount = Config.HttpWorkerCount;
		TransportConfig.ConnectionQueueCapacity = Config.HttpConnectionQueueCapacity;
		if (!Transport->Initialize(
			TransportConfig,
			[this](const FWorldAdapterHttpRequest& Request)
			{
				return HandleHttpRequest(Request);
			},
			OutError))
		{
			Queue->BeginShutdown();
			Backend->BeginShutdown();
			Backend->Shutdown();
			Transport.reset();
			Queue.reset();
			Backend.reset();
			bShuttingDown.store(true);
			return false;
		}
		bInitialized.store(true);
		return true;
	}
	catch (...)
	{
		OutError = "World Adapter Service initialization failed safely";
		BeginShutdown();
		Shutdown();
		return false;
	}
}

std::size_t FWorldAdapterService::Pump(std::size_t MaximumCommands) noexcept
{
	if (!bInitialized.load() || bShuttingDown.load() || !Queue || !Backend || MaximumCommands == 0)
	{
		return 0;
	}
	return Queue->Pump(*Backend, MaximumCommands);
}

void FWorldAdapterService::BeginShutdown() noexcept
{
	if (bShuttingDown.exchange(true))
	{
		return;
	}
	if (Transport)
	{
		Transport->BeginShutdown();
	}
	if (Queue)
	{
		Queue->BeginShutdown();
	}
	if (Backend)
	{
		Backend->BeginShutdown();
	}
}

void FWorldAdapterService::Shutdown() noexcept
{
	if (!Transport && !Queue && !Backend)
	{
		bInitialized.store(false);
		bShuttingDown.store(true);
		return;
	}
	BeginShutdown();
	if (Transport)
	{
		Transport->Shutdown();
	}
	if (Backend)
	{
		Backend->Shutdown();
	}
	Transport.reset();
	Queue.reset();
	Backend.reset();
	bInitialized.store(false);
}

bool FWorldAdapterService::IsRunning() const noexcept
{
	return bInitialized.load() && !bShuttingDown.load() && Transport && Transport->IsRunning();
}

bool FWorldAdapterService::IsShuttingDown() const noexcept
{
	return bShuttingDown.load();
}

std::uint16_t FWorldAdapterService::GetPort() const noexcept
{
	return Transport ? Transport->GetPort() : 0;
}

FWorldAdapterHttpResponse FWorldAdapterService::HandleHttpRequest(const FWorldAdapterHttpRequest& Request) noexcept
{
	try
	{
		if (bShuttingDown.load() || !Queue || !Backend)
		{
			return ErrorResponse(503, MakeError("ADAPTER_UNAVAILABLE", "World Adapter Service is shutting down", {}, true));
		}
		if (!Config.BearerToken.empty())
		{
			const auto Authorization = Request.Headers.find("authorization");
			const std::string Expected = "Bearer " + Config.BearerToken;
			const std::string Received = Authorization == Request.Headers.end() ? std::string() : Authorization->second;
			if (!ConstantTimeEqual(Received, Expected))
			{
				return ErrorResponse(401, MakeError("AUTH_REQUIRED", "Bearer authentication failed"));
			}
		}

		if (Request.Method == "GET" && Request.Path == "/world-adapter/v1/health")
		{
			if (!Request.Body.empty())
			{
				return ErrorResponse(400, MakeError("INVALID_REQUEST", "Health request must not contain a body"));
			}
			FWorldAdapterHealthResponse Response;
			Response.Capabilities = Capabilities;
			return JsonResponse(200, FWorldAdapterJsonProtocol::ToJson(Response));
		}
		if (Request.Method != "POST")
		{
			return ErrorResponse(404, MakeError("NOT_FOUND", "World adapter endpoint not found"));
		}
		if (!IsJsonContentType(Request.Headers))
		{
			return ErrorResponse(415, MakeError("INVALID_CONTENT_TYPE", "Content-Type must be application/json"));
		}

		auto Dispatch = [this](FWorldAdapterCommandRequest CommandRequest) -> std::optional<FWorldAdapterCommandResult>
		{
			auto State = std::make_shared<FWorldAdapterRequestState>();
			const EWorldAdapterEnqueueResult EnqueueResult = Queue->Enqueue({ std::move(CommandRequest), State });
			if (EnqueueResult == EWorldAdapterEnqueueResult::Full)
			{
				return FWorldAdapterCommandResult{ FWorldAdapterExecuteResponse{
					.bOk = false,
					.Error = MakeError("BUSY", "World adapter command queue is full", {}, true),
				} };
			}
			if (EnqueueResult == EWorldAdapterEnqueueResult::ShuttingDown)
			{
				return FWorldAdapterCommandResult{ FWorldAdapterExecuteResponse{
					.bOk = false,
					.Error = MakeError("ADAPTER_UNAVAILABLE", "World adapter command queue is shutting down", {}, true),
				} };
			}
			FWorldAdapterCommandResult Result;
			const EWorldAdapterWaitResult WaitResult = State->WaitFor(Config.RequestTimeout, Result);
			if (WaitResult == EWorldAdapterWaitResult::Completed)
			{
				return Result;
			}
			if (WaitResult == EWorldAdapterWaitResult::TimedOut)
			{
				return FWorldAdapterCommandResult{ FWorldAdapterExecuteResponse{
					.bOk = false,
					.Error = MakeError("TIMEOUT", "World adapter request timed out", {}, true),
				} };
			}
			return FWorldAdapterCommandResult{ FWorldAdapterExecuteResponse{
				.bOk = false,
				.Error = MakeError("ADAPTER_UNAVAILABLE", "World adapter request was cancelled by shutdown", {}, true),
			} };
		};

		if (Request.Path == "/world-adapter/v1/snapshot")
		{
			FWorldAdapterSnapshotRequest Parsed;
			FWorldAdapterError Error;
			if (!FWorldAdapterJsonProtocol::ParseSnapshotRequest(Request.Body, Parsed, Error))
			{
				return ErrorResponse(Error.Code == "REQUEST_TOO_LARGE" ? 413 : 400, std::move(Error));
			}
			const std::optional<FWorldAdapterCommandResult> Result = Dispatch(std::move(Parsed));
			if (!Result || !std::holds_alternative<FWorldAdapterSnapshotResponse>(Result->Response))
			{
				const auto& Failure = std::get<FWorldAdapterExecuteResponse>(Result->Response);
				return ErrorResponse(ErrorStatus(Failure.Error), *Failure.Error);
			}
			const auto& Response = std::get<FWorldAdapterSnapshotResponse>(Result->Response);
			return JsonResponse(ErrorStatus(Response.Error), FWorldAdapterJsonProtocol::ToJson(Response));
		}
		if (Request.Path == "/world-adapter/v1/execute")
		{
			FWorldAdapterExecuteRequest Parsed;
			FWorldAdapterError Error;
			if (!FWorldAdapterJsonProtocol::ParseExecuteRequest(Request.Body, Parsed, Error))
			{
				return ErrorResponse(Error.Code == "REQUEST_TOO_LARGE" ? 413 : 400, std::move(Error));
			}
			const std::optional<FWorldAdapterCommandResult> Result = Dispatch(std::move(Parsed));
			const auto& Response = std::get<FWorldAdapterExecuteResponse>(Result->Response);
			if (Response.RequestId.empty())
			{
				return ErrorResponse(ErrorStatus(Response.Error), *Response.Error);
			}
			return JsonResponse(ErrorStatus(Response.Error), FWorldAdapterJsonProtocol::ToJson(Response));
		}
		if (Request.Path == "/world-adapter/v1/undo")
		{
			FWorldAdapterUndoRequest Parsed;
			FWorldAdapterError Error;
			if (!FWorldAdapterJsonProtocol::ParseUndoRequest(Request.Body, Parsed, Error))
			{
				return ErrorResponse(Error.Code == "REQUEST_TOO_LARGE" ? 413 : 400, std::move(Error));
			}
			const std::optional<FWorldAdapterCommandResult> Result = Dispatch(std::move(Parsed));
			if (!Result || !std::holds_alternative<FWorldAdapterUndoResponse>(Result->Response))
			{
				const auto& Failure = std::get<FWorldAdapterExecuteResponse>(Result->Response);
				return ErrorResponse(ErrorStatus(Failure.Error), *Failure.Error);
			}
			const auto& Response = std::get<FWorldAdapterUndoResponse>(Result->Response);
			return JsonResponse(ErrorStatus(Response.Error), FWorldAdapterJsonProtocol::ToJson(Response));
		}
		return ErrorResponse(404, MakeError("NOT_FOUND", "World adapter endpoint not found"));
	}
	catch (...)
	{
		return ErrorResponse(500, MakeError("INTERNAL_ERROR", "World adapter request failed safely", {}, true));
	}
}

} // namespace Maho
