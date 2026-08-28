#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <Maho.h>
#include <Engine/Engine.h>

#include <string_view>

namespace Maho
{

/**
 * Logging layer — an FEngineLayer (no singleton). Its IInit stage brings the
 * logger up (stdout color + rotating file, honoring a `--log-level` arg); its
 * IShutdown stage flushes + drops it. Logger is public — reach it via
 * FLog::Get().Logger (or the FLog::Info/Warn/Error passthroughs).
 *
 *   Engine.Install(&FLog::Get());   // PreMain 里提前安装
 */
class FLog : public FEngineLayer
{
public:
	/** Process-unique accessor — defined in Log.cpp (in Log.dll). */
	static FLog& Get();

	MAHO_DECLARE_LAYER(FLog);

	/** The shared logger — all engine/service logging routes through it. */
	std::shared_ptr<spdlog::logger> Logger;

	// ── engine init/shutdown stages (FEngineLayer) ──
	void PreInitialize(FEngineBase&) override {}
	void Initialize(FEngineBase& Engine) override;
	void PostInitialize(FEngineBase&) override {}
	void PreShutdown(FEngineBase&) override {}
	void Shutdown(FEngineBase& Engine) override;
	void PostShutdown(FEngineBase&) override {}

	// ── engine tick stages (unused — Log has no per-frame work) ──
	void BeginFrame(FEngineBase&) override {}
	void Tick(FEngineBase&) override {}
	void EndFrame(FEngineBase&) override {}
	void RequestExit(FEngineBase&) override {}

	// fmt-style passthroughs (route through Get().Logger)
	template <typename... Args>
	static void Info(spdlog::format_string_t<Args...> Fmt, Args&&... A)
	{
		Get().Logger->info(Fmt, std::forward<Args>(A)...);
	}
	template <typename... Args>
	static void Warn(spdlog::format_string_t<Args...> Fmt, Args&&... A)
	{
		Get().Logger->warn(Fmt, std::forward<Args>(A)...);
	}
	template <typename... Args>
	static void Error(spdlog::format_string_t<Args...> Fmt, Args&&... A)
	{
		Get().Logger->error(Fmt, std::forward<Args>(A)...);
	}
};

} // namespace Maho

// ── syntax sugar: CORE-logging macros (spdlog-style) ─────────────────────
// Format like the engine core; call after FLog::Get() is installed + initialized:
//
//   MAHO_LOG_CORE_INFO("init {}", name);
//   MAHO_LOG_CORE_ERROR("boom: code={}", code);
#define MAHO_LOG_CORE_TRACE(...)    ::Maho::FLog::Get().Logger->trace(__VA_ARGS__)
#define MAHO_LOG_CORE_DEBUG(...)    ::Maho::FLog::Get().Logger->debug(__VA_ARGS__)
#define MAHO_LOG_CORE_INFO(...)     ::Maho::FLog::Get().Logger->info(__VA_ARGS__)
#define MAHO_LOG_CORE_WARN(...)     ::Maho::FLog::Get().Logger->warn(__VA_ARGS__)
#define MAHO_LOG_CORE_ERROR(...)    ::Maho::FLog::Get().Logger->error(__VA_ARGS__)
#define MAHO_LOG_CORE_CRITICAL(...) ::Maho::FLog::Get().Logger->critical(__VA_ARGS__)
