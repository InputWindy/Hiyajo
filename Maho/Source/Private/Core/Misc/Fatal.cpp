#include <Core/Misc/Fatal.h>

#include <Core/App.h>
#include <Core/Misc/Log.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace Maho
{

namespace
{

std::mutex GFatalMutex;
bool bFatalHandlersInstalled = false;
bool bInsideReportFatal = false;

[[nodiscard]] std::string MakeTimestamp()
{
	using clock = std::chrono::system_clock;
	const auto Now = clock::now();
	const std::time_t Time = clock::to_time_t(Now);
	std::tm Local {};
#if defined(_WIN32)
	localtime_s(&Local, &Time);
#else
	localtime_r(&Time, &Local);
#endif
	char Buffer[64] = {};
	std::snprintf(
		Buffer,
		sizeof(Buffer),
		"%04d-%02d-%02d %02d:%02d:%02d",
		Local.tm_year + 1900,
		Local.tm_mon + 1,
		Local.tm_mday,
		Local.tm_hour,
		Local.tm_min,
		Local.tm_sec);
	return Buffer;
}

void AppendFatalLogFile(const char* Message)
{
	namespace fs = std::filesystem;
	std::error_code ErrorCode;
	fs::create_directories("Saved/Logs", ErrorCode);

	std::ofstream Out("Saved/Logs/Fatal.log", std::ios::app);
	if (!Out)
	{
		return;
	}
	Out << '[' << MakeTimestamp() << "] " << (Message ? Message : "(null)") << '\n';
	Out.flush();
}

void LogCriticalIfLive(const char* Message)
{
	if (!GApp)
	{
		return;
	}

	FLog& Log = GApp->GetLog();
	if (!Log.IsInitialized())
	{
		return;
	}

	try
	{
		if (auto& Core = Log.GetCoreLogger())
		{
			Core->critical("{}", Message ? Message : "(null)");
			Core->flush();
		}
		if (auto& Client = Log.GetClientLogger())
		{
			Client->flush();
		}
	}
	catch (...)
	{
	}
}

} // namespace

[[noreturn]] void ReportFatal(const char* Message)
{
	{
		std::lock_guard<std::mutex> Lock(GFatalMutex);
		if (bInsideReportFatal)
		{
			std::fprintf(stderr, "Maho: recursive ReportFatal: %s\n", Message ? Message : "(null)");
			std::abort();
		}
		bInsideReportFatal = true;
	}

	const char* Text = Message ? Message : "(null)";
	std::fprintf(stderr, "Maho FATAL: %s\n", Text);
	std::fflush(stderr);

	AppendFatalLogFile(Text);
	LogCriticalIfLive(Text);

	std::abort();
}

namespace
{

[[noreturn]] void TerminateHandler()
{
	const char* Message = "std::terminate called (no active exception)";
	std::string Owned;
	try
	{
		if (std::current_exception())
		{
			try
			{
				std::rethrow_exception(std::current_exception());
			}
			catch (const std::exception& Exception)
			{
				Owned = std::string("std::terminate: ") + Exception.what();
				Message = Owned.c_str();
			}
			catch (...)
			{
				Message = "std::terminate: unknown exception";
			}
		}
	}
	catch (...)
	{
		Message = "std::terminate: failed to inspect exception";
	}
	ReportFatal(Message);
}

} // namespace

void InstallFatalHandlers()
{
	std::lock_guard<std::mutex> Lock(GFatalMutex);
	if (bFatalHandlersInstalled)
	{
		return;
	}
	std::set_terminate(TerminateHandler);
	bFatalHandlersInstalled = true;
}

} // namespace Maho
