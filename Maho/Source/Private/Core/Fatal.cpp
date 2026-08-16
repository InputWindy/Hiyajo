#include <Core/Fatal.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#if defined(_WIN32)
#	include <Windows.h>
#endif

namespace Maho
{

namespace
{

std::mutex GFatalMutex;
bool bHandlersInstalled = false;
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
	if (bHandlersInstalled)
	{
		return;
	}
	std::set_terminate(TerminateHandler);
	bHandlersInstalled = true;
}

[[noreturn]] void ReportFatal(const char* Message)
{
#if defined(_WIN32)
	// The console defaults to the OEM codepage (e.g. CP936 on Chinese Windows);
	// switch it to UTF-8 so the UTF-8 fatal text renders correctly. No-op when
	// no console is attached (GUI subsystem).
	SetConsoleOutputCP(CP_UTF8);
#endif

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

	std::abort();
}

} // namespace Maho
