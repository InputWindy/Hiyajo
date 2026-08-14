#pragma once

// DLL export / import (UE-style module boundary).
#if defined(MAHO_BUILD_SHARED)
#	if defined(_WIN32) || defined(_WIN64)
#		if defined(MAHO_EXPORTS)
#			define MAHO_API __declspec(dllexport)
#		else
#			define MAHO_API __declspec(dllimport)
#		endif
#	else
#		define MAHO_API __attribute__((visibility("default")))
#	endif
#else
#	define MAHO_API
#endif

// STL members in exported classes (unique_ptr, string, ...) — safe with matching CRT (/MD).
#if defined(_MSC_VER)
#	pragma warning(disable : 4251)
#endif
