#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "LogApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

namespace Maho
{

/**
 * Logging layer — a thin wrapper over spdlog. Owns a shared, thread-safe
 * console logger; Initialize spins it up, Shutdown flushes+drops it. Any plugin
 * that depends on Log can call FLog::Instance() to log.
 *
 *   FLog::Info("init: {}", name);          // fmt-style format args
 *   FLog::Instance()->warn("warn {} ", x); // raw spdlog access
 */
class FLog
	: public FLayer<>
	, public IPlugin<IMain, IExit>
{
public:
	MAHO_DECLARE_LAYER(FLog, "Log.dll");

	/** The shared logger — route all engine/service logging through it. */
	static std::shared_ptr<spdlog::logger> Instance()
	{
		static std::shared_ptr<spdlog::logger> G = spdlog::stdout_color_mt("Maho");
		return G;
	}

	/** fmt-style convenience (appends a formatted message). */
	template <typename... Args>
	static void Info(spdlog::format_string_t<Args...> Fmt, Args&&... A)
	{
		Instance()->info(Fmt, std::forward<Args>(A)...);
	}
	template <typename... Args>
	static void Warn(spdlog::format_string_t<Args...> Fmt, Args&&... A)
	{
		Instance()->warn(Fmt, std::forward<Args>(A)...);
	}
	template <typename... Args>
	static void Error(spdlog::format_string_t<Args...> Fmt, Args&&... A)
	{
		Instance()->error(Fmt, std::forward<Args>(A)...);
	}

	// The run entry — a logging layer has no per-frame work; returns immediately.
	int Main() override { return 0; }
	void Exit() override {}

	// Install: configure spdlog (pattern + level).
	void Initialize(int, char**) override
	{
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
		spdlog::set_level(spdlog::level::debug);
	}

	// Shutdown: flush + drop the singleton sinks.
	void Shutdown() override
	{
		spdlog::shutdown();
	}
};

} // namespace Maho
