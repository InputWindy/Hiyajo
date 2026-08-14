#include <WorldAdapter/Service/WorldAdapterService.h>
#include <WorldAdapter/Stub/StubBackend.h>
#include <WorldAdapter/WorldAdapter.h>

#if defined(_WIN32)
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <Windows.h>
#endif

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace
{

using namespace std::chrono_literals;

std::atomic<bool> GStopRequested = false;

struct FHarnessOptions
{
	std::uint16_t Port = 8770;
	std::size_t QueueCapacity = Maho::WorldAdapterDefaultQueueCapacity;
	std::chrono::milliseconds RequestTimeout = 5000ms;
	std::size_t IdempotencyCapacity = Maho::WorldAdapterDefaultIdempotencyCapacity;
	std::string BearerToken;
};

bool ParseUnsigned(std::string_view Text, std::uint64_t& OutValue)
{
	if (Text.empty())
	{
		return false;
	}
	const char* Begin = Text.data();
	const char* End = Begin + Text.size();
	const auto Result = std::from_chars(Begin, End, OutValue);
	return Result.ec == std::errc() && Result.ptr == End;
}

bool ReadValue(int& Index, int ArgumentCount, char** Arguments, std::string_view Option, std::string_view& OutValue)
{
	if (Index + 1 >= ArgumentCount)
	{
		std::cerr << "Missing value for " << Option << '\n';
		return false;
	}
	OutValue = Arguments[++Index];
	return true;
}

bool ParseOptions(int ArgumentCount, char** Arguments, FHarnessOptions& OutOptions)
{
#if defined(_WIN32)
	char* EnvironmentToken = nullptr;
	std::size_t EnvironmentTokenLength = 0;
	if (_dupenv_s(&EnvironmentToken, &EnvironmentTokenLength, "MAHO_WORLD_AUTH_TOKEN") == 0 && EnvironmentToken)
	{
		OutOptions.BearerToken = EnvironmentToken;
		std::free(EnvironmentToken);
	}
#else
	if (const char* EnvironmentToken = std::getenv("MAHO_WORLD_AUTH_TOKEN"))
	{
		OutOptions.BearerToken = EnvironmentToken;
	}
#endif

	for (int Index = 1; Index < ArgumentCount; ++Index)
	{
		const std::string_view Option = Arguments[Index];
		if (Option == "--help")
		{
			std::cout
				<< "Usage: MahoWorldAdapterHarness [options]\n"
				<< "  --port <0-65535>\n"
				<< "  --queue-capacity <1-65536>\n"
				<< "  --request-timeout-ms <1-600000>\n"
				<< "  --idempotency-capacity <1-65536>\n"
				<< "  --auth-token <token>\n";
			return false;
		}

		std::string_view Value;
		if (Option == "--auth-token")
		{
			if (!ReadValue(Index, ArgumentCount, Arguments, Option, Value))
			{
				return false;
			}
			OutOptions.BearerToken = Value;
			continue;
		}

		if (Option != "--port" &&
			Option != "--queue-capacity" &&
			Option != "--request-timeout-ms" &&
			Option != "--idempotency-capacity")
		{
			std::cerr << "Unknown option: " << Option << '\n';
			return false;
		}
		if (!ReadValue(Index, ArgumentCount, Arguments, Option, Value))
		{
			return false;
		}

		std::uint64_t Parsed = 0;
		if (!ParseUnsigned(Value, Parsed))
		{
			std::cerr << "Invalid numeric value for " << Option << '\n';
			return false;
		}
		if (Option == "--port")
		{
			if (Parsed > (std::numeric_limits<std::uint16_t>::max)())
			{
				std::cerr << "Port is outside the supported range\n";
				return false;
			}
			OutOptions.Port = static_cast<std::uint16_t>(Parsed);
		}
		else if (Option == "--request-timeout-ms")
		{
			if (Parsed == 0 || Parsed > 600000)
			{
				std::cerr << "Request timeout is outside the supported range\n";
				return false;
			}
			OutOptions.RequestTimeout = std::chrono::milliseconds(Parsed);
		}
		else
		{
			if (Parsed == 0 || Parsed > 65536)
			{
				std::cerr << "Capacity is outside the supported range\n";
				return false;
			}
			if (Option == "--queue-capacity")
			{
				OutOptions.QueueCapacity = static_cast<std::size_t>(Parsed);
			}
			else
			{
				OutOptions.IdempotencyCapacity = static_cast<std::size_t>(Parsed);
			}
		}
	}

	if (OutOptions.BearerToken.size() > 4096)
	{
		std::cerr << "Authentication token is too long\n";
		return false;
	}
	return true;
}

void MonitorStandardInput()
{
	std::string Line;
	while (std::getline(std::cin, Line))
	{
		if (Line == "shutdown" || Line == "quit")
		{
			GStopRequested.store(true);
			return;
		}
	}
	GStopRequested.store(true);
}

#if defined(_WIN32)
BOOL WINAPI HandleConsoleSignal(DWORD Signal)
{
	if (Signal == CTRL_C_EVENT || Signal == CTRL_BREAK_EVENT || Signal == CTRL_CLOSE_EVENT)
	{
		GStopRequested.store(true);
		return TRUE;
	}
	return FALSE;
}
#endif

} // namespace

int main(int ArgumentCount, char** Arguments)
{
	FHarnessOptions Options;
	if (!ParseOptions(ArgumentCount, Arguments, Options))
	{
		return ArgumentCount == 2 && std::string_view(Arguments[1]) == "--help" ? 0 : 2;
	}

#if defined(_WIN32)
	(void)SetConsoleCtrlHandler(HandleConsoleSignal, TRUE);
#endif

	auto Backend = std::make_unique<Maho::FStubWorldBackend>(Maho::FStubWorldBackendConfig{
		.IdempotencyCapacity = Options.IdempotencyCapacity,
	});
	Maho::FWorldAdapterService Service(std::move(Backend));
	Maho::FWorldAdapterServiceConfig Config;
	Config.Port = Options.Port;
	Config.QueueCapacity = Options.QueueCapacity;
	Config.RequestTimeout = Options.RequestTimeout;
	Config.BearerToken = std::move(Options.BearerToken);

	std::string Error;
	if (!Service.Initialize(Config, Error))
	{
		std::cerr << "World Adapter Harness startup failed: " << Error << '\n';
		return 1;
	}

	std::cout
		<< "MAHO_WORLD_ADAPTER_READY {\"host\":\"" << Service.GetHost()
		<< "\",\"port\":" << Service.GetPort()
		<< ",\"protocol\":\"" << Maho::FWorldAdapterBuildInfo::GetProtocolVersion()
		<< "\"}" << std::endl;

	std::thread InputThread(MonitorStandardInput);
	while (!GStopRequested.load())
	{
		const std::size_t Processed = Service.Pump(16);
		if (Processed == 0)
		{
			std::this_thread::sleep_for(1ms);
		}
	}

	Service.BeginShutdown();
	Service.Shutdown();
#if defined(_WIN32)
	if (InputThread.joinable())
	{
		(void)CancelSynchronousIo(InputThread.native_handle());
	}
#endif
	if (InputThread.joinable())
	{
		InputThread.join();
	}
	return 0;
}
