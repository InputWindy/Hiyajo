#pragma once

#include "LogApi.h"
#include <Maho.h>
#include <Engine/Tool.h>

#include <cstdint>

namespace Maho
{

namespace Log
{

enum class ELogLevel : std::uint8_t
{
	Debug = 0,
	Info,
	Warn,
	Error,
	Off,
};

/** Logging extension (spdlog). A plain singleton, driven by the scheduler. */
class MAHO_LOG_API FLogTool : public Maho::TTool<FLogTool>
{
public:
	/** Aggregate identity tags: base + this Tool's own (empty for now). */
	using FTags = TCatch<typename Maho::TTool<FLogTool>::FTags, TTypeList<>>::Type;

	// Services are namespace-level free functions (SetLogLevel/Debug/Info/Warn/Error).

	/** Set the default spdlog level. */
	void Initialize();

	/** Flush and tear down spdlog. */
	void Shutdown();
};

MAHO_LOG_API void SetLogLevel(ELogLevel Level);

// Basic string logging (no fmt in the public header — spdlog stays in Private).
MAHO_LOG_API void Debug(const char* Message);
MAHO_LOG_API void Info(const char* Message);
MAHO_LOG_API void Warn(const char* Message);
MAHO_LOG_API void Error(const char* Message);

} // namespace Log

} // namespace Maho
