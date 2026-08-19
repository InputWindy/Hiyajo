#pragma once

#include "LogApi.h"
#include <Maho.h>

#include <cstdint>

namespace Maho
{

namespace Log
{

/** Log plugin's own drive stage — the host passes it to Execute<Stage>(). */
enum class ELogStage : std::uint8_t
{
	Init = 0,
	Shutdown,
};

enum class ELogLevel : std::uint8_t
{
	Debug = 0,
	Info,
	Warn,
	Error,
	Off,
};

/** Logging extension (spdlog). A plain singleton, driven by the scheduler. */
class MAHO_LOG_API FLog : public Maho::TExtensionList<FLog>
{
public:
	/** Stage dispatch — called by `scheduler.Execute<ELogStage, ...>()`. */
	[[nodiscard]] bool ExecuteStage(ELogStage Stage);
};

MAHO_LOG_API void SetLogLevel(ELogLevel Level);

// Basic string logging (no fmt in the public header — spdlog stays in Private).
MAHO_LOG_API void Debug(const char* Message);
MAHO_LOG_API void Info(const char* Message);
MAHO_LOG_API void Warn(const char* Message);
MAHO_LOG_API void Error(const char* Message);

} // namespace Log

} // namespace Maho
