#pragma once

// DLL export / import (UE-style module boundary).
//
// Five platforms: Windows + Xbox (MSVC/MinGW → __declspec), Linux + Android +
// iOS (GCC/Clang → visibility). Xbox is MSVC-based but does not define
// _WIN32, so _MSC_VER is checked explicitly.
#if defined(MAHO_BUILD_SHARED)
#	if defined(_MSC_VER) || defined(_WIN32) || defined(_WIN64)
#		define MAHO_EXPORT __declspec(dllexport)
#		define MAHO_IMPORT __declspec(dllimport)
#		if defined(MAHO_EXPORTS)
#			define MAHO_API MAHO_EXPORT
#		else
#			define MAHO_API MAHO_IMPORT
#		endif
#	else
#		define MAHO_EXPORT __attribute__((visibility("default")))
#		define MAHO_IMPORT __attribute__((visibility("default")))
#		define MAHO_API MAHO_EXPORT
#	endif
#else
#	define MAHO_EXPORT
#	define MAHO_IMPORT
#	define MAHO_API
#endif

// STL members in exported classes (unique_ptr, string, ...) — safe with matching CRT (/MD).
#if defined(_MSC_VER)
#	pragma warning(disable : 4251)
#endif
