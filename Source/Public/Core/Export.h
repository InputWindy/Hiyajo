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

/**
 * Null-guard statement — evaluate a possibly-null pointer expression ONCE and
 * run the statement only when non-null. The bound name is a local, so the
 * expression is not re-evaluated. Use for optional services reached through a
 * global accessor (e.g. GetLog()).
 *
 *   MAHO_IF_NOT_NULL(::Maho::GetLog(), L)
 *   {
 *       L->Info("ready");
 *   }
 */
#define MAHO_IF_NOT_NULL(PtrExpr, Name)                                  \
	for (auto* Name = (PtrExpr); Name != nullptr; Name = nullptr)
