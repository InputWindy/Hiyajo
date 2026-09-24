#pragma once

#include <Core/BuildConfig.h>
#include <Core/Export.h>

#include <cstdio>

namespace Maho
{

/** Unified fatal path: stderr + Saved/Logs/Fatal.log, then abort. */
[[noreturn]] MAHO_API void ReportFatal(const char* Message);

/** Non-fatal error report: stderr + Saved/Logs/Fatal.log, no abort. */
MAHO_API void ReportError(const char* Message);

/** Record a soft invariant that broke SILENTLY, then break into the debugger when one is attached.
 *  The record happens either way (`ReportError`'s two sinks), the break is a debugger affordance
 *  only -- a run without a debugger continues. See `MAHO_ENSURE_BREAK`. */
MAHO_API void ReportEnsureBreak(const char* Message);

/** Install std::terminate handler once (call from process entry before anything else). */
MAHO_API void InstallFatalHandlers();

} // namespace Maho

// -- UE-style check/ensure macros -------------------------------------------------------
//
// WHICH OF THEM EXIST IS THE BUILD CONFIGURATION'S BUSINESS (`MAHO_DO_*`, see Core/BuildConfig.h):
// Debug/Release keep the checks, Shipping compiles them out entirely -- the expression is not even
// evaluated, so it must not carry side effects (use MAHO_VERIFY for anything that must run).

#if MAHO_DO_CHECK

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

#else

/** Shipping: nothing at all -- the expression is not evaluated (that is the point). */
#define MAHO_CHECK(Expr) ((void)0)

/** Shipping: nothing at all -- the expression is not evaluated. */
#define MAHO_CHECKF(Expr, Fmt, ...) ((void)0)

#endif // MAHO_DO_CHECK

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

#if MAHO_DO_ENSURE

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

/**
 * MAHO_ENSURE_BREAK -- MAHO_ENSURE that ALSO breaks into the debugger the first time it fires.
 *
 * For failures that are SILENT BY DESIGN (the scheduler skipping a frame, a sequence reusing another
 * one's query) a log line is not enough: nobody reads it until after the bug bit, and by then the
 * evidence is a frame that simply has no bars. This reports once (stderr + Saved/Logs/Fatal.log, so a
 * RELEASE run still keeps the record) and then BREAKS -- but only when a debugger is attached, so a
 * build without one keeps running instead of turning a warning into a crash. Execution always
 * continues: this changes what you see, never what the engine does.
 *
 * Shipping (MAHO_DO_ENSURE == 0) compiles the whole thing out, arguments included.
 */
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

/** Shipping: the report is gone too -- but the guard below still has to read. */
#define MAHO_ENSURE(Expr) ((void)0)

/** Shipping: compiled out entirely -- no check, no argument evaluation, no cost. */
#define MAHO_ENSURE_BREAK(Expr, Fmt, ...) ((void)0)

#endif // MAHO_DO_ENSURE

/** MAHO_ENSURE_NOT_NULL -- soft null guard: report once when null, then skip.
 *  Built on MAHO_ENSURE, so the null check itself survives Shipping (only the report goes). */
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
