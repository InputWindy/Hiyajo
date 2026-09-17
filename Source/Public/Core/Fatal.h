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

/**
 * MAHO_IF_NOT_NULL -- silent null guard: evaluate a possibly-null pointer expression
 * ONCE and run the statement only when non-null. The bound name is a local, so the
 * expression is never re-evaluated. Use it for optional services reached through a
 * global accessor (e.g. GetLog()), where "not up yet" is expected rather than a bug.
 *
 *   MAHO_IF_NOT_NULL(::Maho::GetLog(), L)
 *   {
 *       L->Info("ready");
 *   }
 *
 * Sits next to MAHO_ENSURE_NOT_NULL (its reporting sibling) rather than in Export.h:
 * this is control flow, not a module-boundary concern.
 */
#define MAHO_IF_NOT_NULL(PtrExpr, Name)                                               \
	for (auto* Name = (PtrExpr); Name != nullptr; Name = nullptr)
