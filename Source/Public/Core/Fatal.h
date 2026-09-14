#pragma once

#include <Core/Export.h>

#include <cstdio>

namespace Maho
{

/** Unified fatal path: stderr + Saved/Logs/Fatal.log, then abort. */
[[noreturn]] MAHO_API void ReportFatal(const char* Message);

/** Non-fatal error report: stderr + Saved/Logs/Fatal.log, no abort. */
MAHO_API void ReportError(const char* Message);

/** Install std::terminate handler once (call from process entry before anything else). */
MAHO_API void InstallFatalHandlers();

} // namespace Maho

// -- UE-style check/ensure macros -------------------------------------------------------

/**
 * MAHO_CHECK -- hard invariant. False -> ReportFatal (crash). The expression is
 * compiled out in Shipping (use MAHO_VERIFY to keep side effects).
 */
#define MAHO_CHECK(Expr)                                                              \
	do {                                                                              \
		if (!(Expr)) {                                                                \
			::Maho::ReportFatal("MAHO_CHECK failed: " #Expr " at " __FILE__);        \
		}                                                                             \
	} while (0)

/** MAHO_CHECKF -- hard invariant with a formatted message. */
#define MAHO_CHECKF(Expr, Fmt, ...)                                                   \
	do {                                                                              \
		if (!(Expr)) {                                                                \
			char MAHO_CheckMsg[512];                                                  \
			std::snprintf(MAHO_CheckMsg, sizeof(MAHO_CheckMsg), Fmt, ##__VA_ARGS__);  \
			::Maho::ReportFatal(MAHO_CheckMsg);                                       \
		}                                                                             \
	} while (0)

/**
 * MAHO_VERIFY -- like MAHO_CHECK but the expression is ALWAYS evaluated (side
 * effects preserved even when assertions are off).
 */
#define MAHO_VERIFY(Expr)                                                             \
	do {                                                                              \
		if (!(Expr)) {                                                                \
			::Maho::ReportFatal("MAHO_VERIFY failed: " #Expr " at " __FILE__);       \
		}                                                                             \
	} while (0)

/**
 * MAHO_ENSURE -- soft invariant. False -> report ONCE (no crash), then continue.
 * Use for "shouldn't happen but not fatal" -- e.g. a service not yet initialized.
 */
#define MAHO_ENSURE(Expr)                                                             \
	do {                                                                              \
		static bool MAHO_EnsureOnce = false;                                          \
		if (!(Expr) && !MAHO_EnsureOnce) {                                            \
			MAHO_EnsureOnce = true;                                                   \
			::Maho::ReportError("MAHO_ENSURE failed: " #Expr " at " __FILE__);       \
		}                                                                             \
	} while (0)

/** MAHO_ENSURE_NOT_NULL -- soft null guard: report once when null, then skip. */
#define MAHO_ENSURE_NOT_NULL(PtrExpr, Name)                                           \
	MAHO_ENSURE((PtrExpr) != nullptr);                                                \
	for (auto* Name = (PtrExpr); Name != nullptr; Name = nullptr)
