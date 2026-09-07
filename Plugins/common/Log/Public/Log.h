#pragma once

#include <spdlog/fmt/fmt.h>

#include "LogApi.h"
#include <Maho.h>
#include <Core/Fatal.h>
#include <Engine/Engine.h>

#include <memory>
#include <string>

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
 * Logging layer - an FEngineLayer (no singleton). Its Initialize stage brings
 * the logger up (stdout color + rotating file, honoring `--log-level`) and
 * publishes `this` via GetLog(); Shutdown flushes + drops it. The spdlog
 * logger is hidden behind Trace/Debug/Info/Warn/Error/Critical perfect-forward
 * templates - callers never see spdlog types.
 *
 *   Engine.Install("Log.dll");   // install early in PreMain
 */
class FLog : public FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>
{
public:
	MAHO_DECLARE_LAYER(FLog, "Log.dll");

	FLog();
	~FLog() override;

	// -- logging passthroughs (perfect-forward, fmt compile-time checked) --
	template <typename... Args>
	void Trace(fmt::format_string<Args...> Fmt, Args&&... A)
	{
		LogLine(ELogLevel::Trace, fmt::format(Fmt, std::forward<Args>(A)...));
	}
	template <typename... Args>
	void Debug(fmt::format_string<Args...> Fmt, Args&&... A)
	{
		LogLine(ELogLevel::Debug, fmt::format(Fmt, std::forward<Args>(A)...));
	}
	template <typename... Args>
	void Info(fmt::format_string<Args...> Fmt, Args&&... A)
	{
		LogLine(ELogLevel::Info, fmt::format(Fmt, std::forward<Args>(A)...));
	}
	template <typename... Args>
	void Warn(fmt::format_string<Args...> Fmt, Args&&... A)
	{
		LogLine(ELogLevel::Warn, fmt::format(Fmt, std::forward<Args>(A)...));
	}
	template <typename... Args>
	void Error(fmt::format_string<Args...> Fmt, Args&&... A)
	{
		LogLine(ELogLevel::Error, fmt::format(Fmt, std::forward<Args>(A)...));
	}
	template <typename... Args>
	void Critical(fmt::format_string<Args...> Fmt, Args&&... A)
	{
		LogLine(ELogLevel::Critical, fmt::format(Fmt, std::forward<Args>(A)...));
	}

private:
	// -- engine init/shutdown stages (scheduler-only) --
	void PreInitialize(FEngineBase&) override {}
	void Initialize(FEngineBase& Engine) override;
	void PostInitialize(FEngineBase&) override {}
	void PreShutdown(FEngineBase&) override {}
	void Shutdown(FEngineBase& Engine) override;
	void PostShutdown(FEngineBase&) override {}

	void LogLine(ELogLevel Level, std::string Message);

	std::shared_ptr<spdlog::logger> Logger;   // incomplete type; dtor in Log.cpp
};

} // namespace Maho

// -- syntax sugar: CORE-logging macros (fmt-style) -------------------------
// Format like the engine core; call after the Log layer is installed + initialized.
// GLog may be null before the Log layer's Initialize runs - macros report once
// (ensure) and skip.
//
//   MAHO_LOG_CORE_INFO("init {}", name);
//   MAHO_LOG_CORE_ERROR("boom: code={}", code);
#define MAHO_LOG_CORE_TRACE(...)    MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Trace(__VA_ARGS__);
#define MAHO_LOG_CORE_DEBUG(...)    MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Debug(__VA_ARGS__);
#define MAHO_LOG_CORE_INFO(...)     MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Info(__VA_ARGS__);
#define MAHO_LOG_CORE_WARN(...)     MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Warn(__VA_ARGS__);
#define MAHO_LOG_CORE_ERROR(...)    MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Error(__VA_ARGS__);
#define MAHO_LOG_CORE_CRITICAL(...) MAHO_ENSURE_NOT_NULL(::Maho::GetLog(), L) L->Critical(__VA_ARGS__);
