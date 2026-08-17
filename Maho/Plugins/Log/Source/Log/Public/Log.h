#pragma once

#include "LogApi.h"
#include <Engine.h>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

#include <utility>

namespace Maho
{

namespace Log
{

enum class ELogLevel : std::uint8_t { Debug = 0, Info, Warn, Error, Off };

/** Logging extension (spdlog). Pre-app toolkit (driven by EToolStage). */
class MAHO_LOG_API FLogger : public TExtension<EToolStage, FLogger>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

protected:
	friend TSingleton<FLogger>;
	FLogger() = default;
};

// Free logging functions (forward to FLogger::Get()).
template <typename... TArgs>
void Debug(fmt::format_string<TArgs...> Fmt, TArgs&&... Args)
{
	spdlog::debug(Fmt, std::forward<TArgs>(Args)...);
}

template <typename... TArgs>
void Info(fmt::format_string<TArgs...> Fmt, TArgs&&... Args)
{
	spdlog::info(Fmt, std::forward<TArgs>(Args)...);
}

template <typename... TArgs>
void Warn(fmt::format_string<TArgs...> Fmt, TArgs&&... Args)
{
	spdlog::warn(Fmt, std::forward<TArgs>(Args)...);
}

template <typename... TArgs>
void Error(fmt::format_string<TArgs...> Fmt, TArgs&&... Args)
{
	spdlog::error(Fmt, std::forward<TArgs>(Args)...);
}

MAHO_LOG_API void SetLogLevel(ELogLevel Level);

} // namespace Log

} // namespace Maho
