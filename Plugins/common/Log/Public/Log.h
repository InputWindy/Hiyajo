#pragma once

#include <Core/BuildConfig.h>

// fmt stays unconditional: the passthrough templates below name fmt::format_string in their
// SIGNATURES, and call sites (e.g. `MAHO_IF_NOT_NULL(GetLog(), L) { L->Warn("..."); }`) must keep
// compiling in every configuration. What the configuration removes is the BODY -- see the templates.
#include <spdlog/fmt/fmt.h>

#include "LogApi.h"
#include <Maho.h>
#include <Core/Delegate.h>
#include <Core/Fatal.h>
#include <Engine/Engine.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace spdlog
{
class logger;
}

namespace Maho
{

class FLog;

/** Global log instance accessor - returns FLog* (cross-DLL via function, no bare variable export). */
MAHO_LOG_API FLog* GetLog();

/** Log level (type-erases spdlog level; the header never exposes spdlog). */
enum class ELogLevel
{
	Trace,
	Debug,
	Info,
	Warn,
	Error,
	Critical,
};

/**
 * A single captured log line. Listener callbacks receive this by const ref;
 * it is a value snapshot (the string is copied), so a subscriber can retain it.
 */
struct FLogMessage
{
	ELogLevel  Level;
	std::string Category;   // source category; empty = "None" (legacy callers)
	std::string Message;
};

/**
 * Logging layer - an FEngineLayer (no singleton). Its Initialize stage brings
 * the logger up (stdout color + rotating file, honoring `--log-level`) and
 * publishes `this` via GetLog(); Shutdown flushes + drops it. The spdlog
 * logger is hidden behind Trace/Debug/Info/Warn/Error/Critical perfect-forward
 * templates - callers never see spdlog types.
 *
 *   Engine.Install<FLog>();   // install early in PreMain   (or Install(ApplyModuleExtension("FLog")))
 */
class FLog : public FFrameExtension, public IPipeline<IInit, ITick, IShutdown>
{
public:
	MAHO_DECLARE_FRAME(FLog);

	FLog();
	~FLog() override;

	// -- logging passthroughs (perfect-forward, fmt compile-time checked) --
	//
	// IN SHIPPING (MAHO_WITH_LOGGING == 0) THE BODIES DISAPPEAR: the call sites still compile (the
	// signatures keep fmt::format_string), but no message is formatted, delivered or sunk. And since
	// Shipping does not publish `GetLog()` either, the usual `MAHO_IF_NOT_NULL(GetLog(), L)` sites do
	// not even reach here.
	template <typename... Args>
	void Trace(fmt::format_string<Args...> Fmt, Args&&... A)
	{
#if MAHO_WITH_LOGGING
		LogLine(ELogLevel::Trace, fmt::format(Fmt, std::forward<Args>(A)...));
#else
		(void)Fmt;
#endif
	}
	template <typename... Args>
	void Debug(fmt::format_string<Args...> Fmt, Args&&... A)
	{
#if MAHO_WITH_LOGGING
		LogLine(ELogLevel::Debug, fmt::format(Fmt, std::forward<Args>(A)...));
#else
		(void)Fmt;
#endif
	}
	template <typename... Args>
	void Info(fmt::format_string<Args...> Fmt, Args&&... A)
	{
#if MAHO_WITH_LOGGING
		LogLine(ELogLevel::Info, fmt::format(Fmt, std::forward<Args>(A)...));
#else
		(void)Fmt;
#endif
	}
	template <typename... Args>
	void Warn(fmt::format_string<Args...> Fmt, Args&&... A)
	{
#if MAHO_WITH_LOGGING
		LogLine(ELogLevel::Warn, fmt::format(Fmt, std::forward<Args>(A)...));
#else
		(void)Fmt;
#endif
	}
	template <typename... Args>
	void Error(fmt::format_string<Args...> Fmt, Args&&... A)
	{
#if MAHO_WITH_LOGGING
		LogLine(ELogLevel::Error, fmt::format(Fmt, std::forward<Args>(A)...));
#else
		(void)Fmt;
#endif
	}
	template <typename... Args>
	void Critical(fmt::format_string<Args...> Fmt, Args&&... A)
	{
#if MAHO_WITH_LOGGING
		LogLine(ELogLevel::Critical, fmt::format(Fmt, std::forward<Args>(A)...));
#else
		(void)Fmt;
#endif
	}

	// -- category-aware logging (UE Output Log style) -------------------------
	// The category rides on the message so listeners (e.g. the Editor Console
	// category filter tree) can aggregate and filter. Legacy Trace/Debug/... use
	// an empty category; use this when you want a named source tag.
	template <typename... Args>
	void Log(ELogLevel Level, std::string_view Category, fmt::format_string<Args...> Fmt, Args&&... A)
	{
#if MAHO_WITH_LOGGING
		LogLine(Level, std::string(Category), fmt::format(Fmt, std::forward<Args>(A)...));
#else
		(void)Level;
		(void)Category;
		(void)Fmt;
#endif
	}

	// Live log stream, delivered by Broadcast() on the emitting thread. Thread-safe
	// (Core TMulticastEvent owns its lock); subscribers unsubscribe in their own
	// Shutdown so this strand is empty before the Log layer tears down.
	TMulticastEvent<void(const FLogMessage&)> OnLog;
private:
	// -- engine stages (scheduler-only) --
	void Initialize(FEngineBase& Engine, FEngineContext& Frame) override;
	void Tick(FEngineBase& Engine, FEngineContext& Frame) override;
	void Shutdown(FEngineBase& Engine, FEngineContext& Frame) override;

	void LogLine(ELogLevel Level, std::string Message);
	void LogLine(ELogLevel Level, std::string Category, std::string Message);

	std::shared_ptr<spdlog::logger> Logger;   // incomplete type; dtor in Log.cpp

	/** Last `r.Trace` value this layer applied, so Tick only touches the profiler's switch when the
	 *  CVar actually changed (-1 = nothing applied yet, which forces the first Tick to sync it). */
	int LastTraceRequest = -1;

};

} // namespace Maho

// -- syntax sugar: CORE-logging macros (fmt-style) -------------------------
// Format like the engine core; call after the Log layer is installed + initialized.
// GLog may be null before the Log layer's Initialize runs - macros report once
// (ensure) and skip.
//
//   MAHO_LOG_CORE_INFO("init {}", name);
//   MAHO_LOG_CORE_ERROR("boom: code={}", code);
//
// IN SHIPPING (MAHO_WITH_LOGGING == 0, see Core/BuildConfig.h) THEY EXPAND TO NOTHING: logging is a
// diagnostic the release build does not carry, so the arguments are not even evaluated (do not put
// required side effects in a log call). ReportFatal/ReportError still work -- a crash must stay
// explicable.
#if MAHO_WITH_LOGGING
#define MAHO_LOG_CORE_TRACE(...)    MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Trace(__VA_ARGS__);
#define MAHO_LOG_CORE_DEBUG(...)    MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Debug(__VA_ARGS__);
#define MAHO_LOG_CORE_INFO(...)     MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Info(__VA_ARGS__);
#define MAHO_LOG_CORE_WARN(...)     MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Warn(__VA_ARGS__);
#define MAHO_LOG_CORE_ERROR(...)    MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Error(__VA_ARGS__);
#define MAHO_LOG_CORE_CRITICAL(...) MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Critical(__VA_ARGS__);
// Category-aware variant (UE Output Log style) - tag rides on the message:
//   MAHO_LOG(ELogLevel::Info, "LogRender", "init {}", id);
#define MAHO_LOG(Level, Category, ...) MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Log(Level, Category, __VA_ARGS__);
#else
/** Shipping: the log is not compiled; nothing here is evaluated. */
#define MAHO_LOG_CORE_TRACE(...) ((void)0)
/** Shipping: the log is not compiled; nothing here is evaluated. */
#define MAHO_LOG_CORE_DEBUG(...) ((void)0)
/** Shipping: the log is not compiled; nothing here is evaluated. */
#define MAHO_LOG_CORE_INFO(...) ((void)0)
/** Shipping: the log is not compiled; nothing here is evaluated. */
#define MAHO_LOG_CORE_WARN(...) ((void)0)
/** Shipping: the log is not compiled; nothing here is evaluated. */
#define MAHO_LOG_CORE_ERROR(...) ((void)0)
/** Shipping: the log is not compiled; nothing here is evaluated. */
#define MAHO_LOG_CORE_CRITICAL(...) ((void)0)
/** Shipping: the log is not compiled; nothing here is evaluated. */
#define MAHO_LOG(Level, Category, ...) ((void)0)
#endif // MAHO_WITH_LOGGING
