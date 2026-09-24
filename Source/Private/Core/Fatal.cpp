#include <Core/Fatal.h>

#include <chrono>
#include <csignal>
#include <cstdint>
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

#if defined(_WIN32)
namespace
{

/** Raw return addresses, newest first, printed as a list. Without a PDB the numbers are the point:
 *  a crash that says WHERE (a module offset) is already most of the way to a cause -- symbolise with
 *  `dumpbin /disasm` + the nearest public symbol, which is how the trace's flush crash was found. */
void PrintAddressStack(const char* Why)
{
	void* Frames[40] = {};
	const USHORT Count = RtlCaptureStackBackTrace(1, 40, Frames, nullptr);
	std::fprintf(stderr, "Maho CRASH (%s): %u frames\n", Why, static_cast<unsigned>(Count));
	for (USHORT i = 0; i < Count; ++i)
	{
		std::fprintf(stderr, "  #%02u %p\n", static_cast<unsigned>(i), Frames[i]);
	}
	std::fflush(stderr);
	std::abort();
}

/** The CRT's invalid-parameter path. It normally goes straight to __fastfail, which no SEH filter and
 *  no terminate handler ever sees -- so a bad argument (a null where a string was wanted, a length
 *  overrun) killed release builds with nothing but "0xc0000409 in ucrtbase". This handler runs FIRST
 *  and is handed the offending call's function / file / line / expression, so the message names the
 *  call site instead of the CRT internals. */
void OnInvalidParameter(const wchar_t* Expression, const wchar_t* Function, const wchar_t* File,
	unsigned int Line, std::uintptr_t /*Reserved*/)
{
	std::fprintf(stderr, "Maho CRASH (invalid parameter): %ls\n  at %ls:%u\n  expr: %ls\n",
		Function != nullptr ? Function : L"(unknown)",
		File != nullptr ? File : L"(unknown)", Line,
		Expression != nullptr ? Expression : L"(unknown)");
	std::fflush(stderr);
	PrintAddressStack("invalid parameter");
}

/** abort() -- reached by ReportFatal, by the CRT's own bail-outs and by std::terminate paths that do
 *  not go through the handler above. Raising SIGABRT is abort's first step, so a handler here can
 *  still speak before the process dies. */
void OnAbort(int)
{
	std::fprintf(stderr, "Maho CRASH (SIGABRT)\n");
	std::fflush(stderr);
	PrintAddressStack("SIGABRT");
}

/** A plain access violation: the filter runs before the process is torn down. (Not reached by
 *  __fastfail -- see OnInvalidParameter for the CRT's fast paths.) */
LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* Info)
{
	std::fprintf(stderr, "Maho CRASH (exception %08lx) at %p\n",
		Info != nullptr ? Info->ExceptionRecord->ExceptionCode : 0ul,
		Info != nullptr ? Info->ExceptionRecord->ExceptionAddress : nullptr);
	std::fflush(stderr);
	PrintAddressStack("exception");
	return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace
#endif

void InstallFatalHandlers()
{
	std::lock_guard<std::mutex> Lock(GFatalMutex);
	if (bHandlersInstalled)
	{
		return;
	}
	std::set_terminate(TerminateHandler);
#if defined(_WIN32)
	// The rest of the "explain the crash" surface: terminate covers C++ exceptions, and these cover
	// the three ways the process dies WITHOUT going through it (a bad CRT parameter, abort, and a
	// fault) -- each printing its own stack, because a hard crash has no stack to walk later.
	_set_invalid_parameter_handler(OnInvalidParameter);
	std::signal(SIGABRT, OnAbort);
	SetUnhandledExceptionFilter(OnUnhandledException);
#endif
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

void ReportError(const char* Message)
{
	// Serialize concurrent reports -- a plugin exception can be reported from any
	// pool worker, and the console/log append must not interleave.
	std::lock_guard<std::mutex> Lock(GFatalMutex);
	const char* Text = Message ? Message : "(null)";
	std::fprintf(stderr, "Maho ERROR: %s\n", Text);
	std::fflush(stderr);

	AppendFatalLogFile(Text);
}

} // namespace Maho
