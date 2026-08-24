#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Core/Singleton.h>
#include <Maho.h>

#include <string_view>

namespace Maho
{

/**
 * Logging singleton — a CRTP singleton (T::Get()) wrapping spdlog. Initiate
 * configures the thread-safe stdout-color logger (honoring a `--log-level` arg
 * if present); Shutdown flushes+drops it. Logger is public — just reach it via
 * FLog::Get().Logger (or the FLog::Info/Warn/Error passthroughs).
 *
 *   FLog::Get().Initiate(argc, argv);
 *   FLog::Info("init: {}", name);
 *   FLog::Error("boom: code={}", code);
 */
class FLog : public TSingleton<FLog>
{
public:
	/** The shared logger — all engine/service logging routes through it. */
	std::shared_ptr<spdlog::logger> Logger;

	/** Bring the logger up (ISingleton::Initiate). */
	void Initiate(int, char** Argv) override
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
