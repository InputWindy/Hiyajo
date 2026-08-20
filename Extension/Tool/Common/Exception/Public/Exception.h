#pragma once

#include "ExceptionApi.h"
#include <Maho.h>
#include <Engine/PluginTemplates.h>

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace Maho
{

namespace Exception
{

/** Exception plugin's own drive stage — the host passes it to Execute<Stage>(). */
enum class EExceptionStage : std::uint8_t
{
	Init = 0,
	Shutdown,
};

/**
 * Non-fatal exception handling extension.
 *
 * ReportException broadcasts the message to every subscriber — logging,
 * telemetry, crash reporters hook OnException. Nothing aborts here; fatal
 * errors go through Core/Fatal instead.
 *
 *   FException::Get().OnException.Add([](const std::string& M) {
 *       Maho::Log::Error("exception: {}", M);
 *   });
 *   FException::Get().ReportException("failed to load texture");
 */
class MAHO_EXCEPTION_API FException : public Maho::TTool<FException>
{
public:
	/** Stage dispatch — called by `scheduler.Execute<EExceptionStage, ...>()`. */
	[[nodiscard]] bool ExecuteStage(EExceptionStage Stage);

	/** Report a non-fatal exception (broadcasts to OnException). */
	void ReportException(std::string_view Message);

	/** Report from a std::exception (forwards what()). */
	void ReportException(const std::exception& Error);

	/** Subscribers receive every reported exception message. */
	Maho::TMulticastDelegate<void(const std::string&)> OnException;
};

} // namespace Exception

} // namespace Maho
