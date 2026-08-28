#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Core/Singleton.h>
#include <Maho.h>
#include <Maho.h>

#include <string_view>

namespace Maho
{

/**
 * Logging singleton �?a CRTP singleton (T::Get()) wrapping spdlog. Initialize
 * configures the thread-safe stdout-color logger (honoring a `--log-level` arg
 * if present); Shutdown flushes+drops it. Logger is public �?just reach it via
 * FLog::Get().Logger (or the FLog::Info/Warn/Error passthroughs).
 *
 *   FLog::Get().Initialize(argc, argv);
 *   FLog::Info("init: {}", name);
 *   FLog::Error("boom: code={}", code);
 */
class FLog
	: public TSingleton<FLog>
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Process-unique accessor �?defined in Log.cpp (in Log.dll). */
	static FLog& Get();

	/** The shared logger �?all engine/service logging routes through it. */
	std::shared_ptr<spdlog::logger> Logger;

	/** Bring the logger up (ISingleton::Initialize). */
	void Initialize(int, char** Argv) override
	{
		Logger = spdlog::stdout_color_mt("Maho");
		Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
		spdlog::level::level_enum Lv = spdlog::level::debug;
		if (Argv) // allow --log-level=trace|debug|info|warn|error
		{
			for (int i = 0; Argv[i]; ++i)
			{
				if (const char* V = Argv[i][0] == '-' ? Argv[i] + 1 : nullptr; V && std::string_view(V) == "log-level" && Argv[i + 1])
				{
					Lv = spdlog::level::from_str(Argv[i + 1]);
				}
			}
		}
		Logger->set_level(Lv);
	}

	/** Flush + drop the sinks (ISingleton::Shutdown). */
	void Shutdown() override
	{
		spdlog::shutdown();
		Logger.reset();
	}

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
// Format like the engine core; call after FLog::Get().Initialize(argc, argv):
//
//   MAHO_LOG_CORE_INFO("init {}", name);
//   MAHO_LOG_CORE_ERROR("boom: code={}", code);
#define MAHO_LOG_CORE_TRACE(...)    ::Maho::FLog::Get().Logger->trace(__VA_ARGS__)
#define MAHO_LOG_CORE_DEBUG(...)    ::Maho::FLog::Get().Logger->debug(__VA_ARGS__)
#define MAHO_LOG_CORE_INFO(...)     ::Maho::FLog::Get().Logger->info(__VA_ARGS__)
#define MAHO_LOG_CORE_WARN(...)     ::Maho::FLog::Get().Logger->warn(__VA_ARGS__)
#define MAHO_LOG_CORE_ERROR(...)    ::Maho::FLog::Get().Logger->error(__VA_ARGS__)
#define MAHO_LOG_CORE_CRITICAL(...) ::Maho::FLog::Get().Logger->critical(__VA_ARGS__)
