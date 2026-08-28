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

/** 进程全局日志实例访问器 —— 返回 FLog*（跨 DLL 经函数，不用裸变量导出）。 */
MAHO_LOG_API FLog* GetLog();

/** 日志级别（对 spdlog level 的类型擦除，头文件不暴露 spdlog）。 */
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
 * Logging layer — an FEngineLayer (no singleton). Its Initialize stage brings
 * the logger up (stdout color + rotating file, honoring `--log-level`) and
 * publishes `this` via GetLog(); Shutdown flushes + drops it. The spdlog
 * logger is hidden behind Trace/Debug/Info/Warn/Error/Critical perfect-forward
 * templates — callers never see spdlog types.
 *
 *   Engine.Install(&Log);   // PreMain 里提前安装
 */
class FLog : public FEngineLayer
{
public:
	MAHO_DECLARE_ENGINE_LAYER(FLog, "Log.dll");

	FLog();
	~FLog() override;

	// ── engine init/shutdown stages ──
	void Initialize(FEngineBase& Engine) override;
	void Shutdown(FEngineBase& Engine) override;

	// ── logging passthroughs (perfect-forward, fmt compile-time checked) ──
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
	void LogLine(ELogLevel Level, std::string Message);

	std::shared_ptr<spdlog::logger> Logger;   // 不完整类型；析构在 Log.cpp
};

} // namespace Maho

// ── syntax sugar: CORE-logging macros (fmt-style) ────────────────────────
// Format like the engine core; call after the Log layer is installed + initialized.
// GLog may be null before the Log layer's Initialize runs — macros report once
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
