#pragma once

#include <Core/Export.h>

#include <cstdio>

namespace Maho
{

[[noreturn]] MAHO_API void ReportFatal(const char* Message);

MAHO_API void ReportError(const char* Message);

MAHO_API void ReportEnsureBreak(const char* Message);

MAHO_API void InstallFatalHandlers();

}


#if MAHO_DO_CHECK

#define MAHO_CHECK(Expr)                                                              \
	do {                                                                              \
		if (!(Expr)) {                                                                \
			::Maho::ReportFatal("MAHO_CHECK failed: " #Expr " at " __FILE__);        \
		}                                                                             \
	} while (0)

#define MAHO_CHECKF(Expr, Fmt, ...)                                                   \
	do {                                                                              \
		if (!(Expr)) {                                                                \
			char MAHO_CheckMsg[512];                                                  \
			std::snprintf(MAHO_CheckMsg, sizeof(MAHO_CheckMsg), Fmt, ##__VA_ARGS__);  \
			::Maho::ReportFatal(MAHO_CheckMsg);                                       \
		}                                                                             \
	} while (0)

#else

#define MAHO_CHECK(Expr) ((void)0)

#define MAHO_CHECKF(Expr, Fmt, ...) ((void)0)

#endif

#define MAHO_VERIFY(Expr)                                                             \
	do {                                                                              \
		if (!(Expr)) {                                                                \
			::Maho::ReportFatal("MAHO_VERIFY failed: " #Expr " at " __FILE__);       \
		}                                                                             \
	} while (0)

#if MAHO_DO_ENSURE

#define MAHO_ENSURE(Expr)                                                             \
	do {                                                                              \
		static bool MAHO_EnsureOnce = false;                                          \
		if (!(Expr) && !MAHO_EnsureOnce) {                                            \
			MAHO_EnsureOnce = true;                                                   \
			::Maho::ReportError("MAHO_ENSURE failed: " #Expr " at " __FILE__);       \
		}                                                                             \
	} while (0)

#define MAHO_ENSURE_BREAK(Expr, Fmt, ...)                                             \
	do {                                                                              \
		static bool MAHO_EnsureBreakOnce = false;                                     \
		if (!(Expr) && !MAHO_EnsureBreakOnce) {                                       \
			MAHO_EnsureBreakOnce = true;                                              \
			char MAHO_EnsureBreakMsg[512];                                            \
			std::snprintf(MAHO_EnsureBreakMsg, sizeof(MAHO_EnsureBreakMsg), Fmt,      \
				##__VA_ARGS__);                                                        \
			::Maho::ReportEnsureBreak(MAHO_EnsureBreakMsg);                           \
		}                                                                             \
	} while (0)

#else

#define MAHO_ENSURE(Expr) ((void)0)

#define MAHO_ENSURE_BREAK(Expr, Fmt, ...) ((void)0)

#endif

#define MAHO_ENSURE_NOT_NULL(PtrExpr, Name)                                           \
	MAHO_ENSURE((PtrExpr) != nullptr);                                                \
	for (auto* Name = (PtrExpr); Name != nullptr; Name = nullptr)

#define MAHO_IF_NOT_NULL(PtrExpr, Name)                                               \
	for (auto* Name = (PtrExpr); Name != nullptr; Name = nullptr)
