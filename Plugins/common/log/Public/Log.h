#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Core/Singleton.h>
#include <Maho.h>

namespace Maho
{

/**
 * Logging singleton — a CRTP singleton (T::Get()) wrapping spdlog. Initiate
 * configures the thread-safe stdout-color logger; Shutdown flushes+drops it
 * (both are ISingleton's fixed lifecycle, driven via Select<ISingleton>().ForEach).
 * Any plugin depending on Log can log straight through FLog:
 *
 *   FLog::InitLog();                  // or driven by the engine's singleton init
 *   FLog::Info("init: {}", name);
 *   FLog::Error("boom: code={}", code);
 */
class FLog : public TSingleton<FLog>
{
public:
	/** Bring the logger up (ISingleton::Initiate). */
	void Initiate() override
	{
		Logger = spdlog::stdout_color_mt("Maho");
		Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
		Logger->set_level(spdlog::level::debug);
	}

	/** Flush + drop the sinks (ISingleton::Shutdown). */
	void Shutdown() override
	{
		spdlog::shutdown();
		Logger.reset();
	}

	/** The shared logger. */
	static std::shared_ptr<spdlog::logger>& Instance()
	{
		static thread_local auto Log = Get().Logger;
		return Log;
	}

	// fmt-style passthroughs
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

private:
	std::shared_ptr<spdlog::logger> Logger;
};

} // namespace Maho
