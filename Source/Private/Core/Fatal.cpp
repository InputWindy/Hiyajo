#include <Core/Fatal.h>

#ifndef MAHO_BUILD_TYPE
#	error "MAHO_BUILD_TYPE is undefined: this target was not configured by the generator (Tools/maho_tools.py), which is the only definition site of the build-shape macros. Build through the generated project."
#endif
#ifndef MAHO_BUILD_TARGET
#	error "MAHO_BUILD_TARGET is undefined: this target was not configured by the generator (Tools/maho_tools.py). Build through the generated project."
#endif
#if defined(MAHO_EDITOR_BUILD) && defined(MAHO_BUILD_SHIPPING)
#	error "Shipping x Editor is not a valid combination -- ship an editor with Release x Editor."
#endif

#include <Core/FrameGraph.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

}

#if defined(_WIN32)
namespace
{

void PrintAddressStack(const char* Why)
{
	void* Frames[40] = {};
	const USHORT Count = RtlCaptureStackBackTrace(1, 40, Frames, nullptr);
	std::fprintf(stderr, "Maho CRASH (%s): %u frames\n", Why, static_cast<unsigned>(Count));

	const char* Node = GetCurrentGraphNode();
	if (Node != nullptr && Node[0] != 0)
	{
		std::fprintf(stderr, "  during node %s\n", Node);
	}

	for (USHORT i = 0; i < Count; ++i)
	{
		char Where[MAX_PATH + 32] = "?";
		char ModulePath[MAX_PATH] = {};
		HMODULE Module = nullptr;
		if (GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(Frames[i]), &Module) != 0
			&& Module != nullptr
			&& GetModuleFileNameA(Module, ModulePath, MAX_PATH) != 0)
		{
			const char* Name = std::strrchr(ModulePath, '\\');
			Name = (Name != nullptr) ? Name + 1 : ModulePath;
			const unsigned long long Offset = static_cast<unsigned long long>(
				reinterpret_cast<std::uintptr_t>(Frames[i]) - reinterpret_cast<std::uintptr_t>(Module));
			std::snprintf(Where, sizeof(Where), "%s+0x%llX", Name, Offset);
		}
		std::fprintf(stderr, "  #%02u %p  %s\n", static_cast<unsigned>(i), Frames[i], Where);
	}
	std::fflush(stderr);
	std::abort();
}

void OnInvalidParameter(const wchar_t* Expression, const wchar_t* Function, const wchar_t* File,
	unsigned int Line, std::uintptr_t )
{
	std::fprintf(stderr, "Maho CRASH (invalid parameter): %ls\n  at %ls:%u\n  expr: %ls\n",
		Function != nullptr ? Function : L"(unknown)",
		File != nullptr ? File : L"(unknown)", Line,
		Expression != nullptr ? Expression : L"(unknown)");
	std::fflush(stderr);
	PrintAddressStack("invalid parameter");
}

void OnAbort(int)
{
	std::fprintf(stderr, "Maho CRASH (SIGABRT)\n");
	std::fflush(stderr);
	PrintAddressStack("SIGABRT");
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* Info)
{
	std::fprintf(stderr, "Maho CRASH (exception %08lx) at %p\n",
		Info != nullptr ? Info->ExceptionRecord->ExceptionCode : 0ul,
		Info != nullptr ? Info->ExceptionRecord->ExceptionAddress : nullptr);
	std::fflush(stderr);
	PrintAddressStack("exception");
	return EXCEPTION_EXECUTE_HANDLER;
}

}
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
	_set_invalid_parameter_handler(OnInvalidParameter);
	std::signal(SIGABRT, OnAbort);
	SetUnhandledExceptionFilter(OnUnhandledException);
#endif
	bHandlersInstalled = true;
}

[[noreturn]] void ReportFatal(const char* Message)
{
#if defined(_WIN32)
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
	std::lock_guard<std::mutex> Lock(GFatalMutex);
	const char* Text = Message ? Message : "(null)";
	std::fprintf(stderr, "Maho ERROR: %s\n", Text);
	std::fflush(stderr);

	AppendFatalLogFile(Text);
}

void ReportEnsureBreak(const char* Message)
{
	ReportError(Message);

#if defined(_WIN32) && defined(_MSC_VER)
	if (IsDebuggerPresent())
	{
		__debugbreak();
	}
#endif
}

}
