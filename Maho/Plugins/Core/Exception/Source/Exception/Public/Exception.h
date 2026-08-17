#pragma once

#include "ExceptionApi.h"
#include <Core/Core.h>

#include <exception>
#include <string>
#include <string_view>

namespace Maho
{

namespace Exception
{

/**
 * Non-fatal exception handling extension (driven by EEngineStage).
 *
 * ReportException broadcasts the message to every subscriber — logging,
 * telemetry, crash reporters hook OnException. Nothing aborts here; fatal
 * errors go through Core/Fatal instead.
 *
 *   FException::Get().OnException.Bind([](const std::string& M) {
 *       Maho::Log::Error("exception: {}", M);
 *   });
 *   FException::Get().ReportException("failed to load texture");
 */
class MAHO_EXCEPTION_API FException : public TExtension<EEngineStage, FException>
{
public:
	[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

	/** Report a non-fatal exception (broadcasts to OnException). */
	void ReportException(std::string_view Message);

	/** Report from a std::exception (forwards what()). */
	void ReportException(const std::exception& Error);

	/** Subscribers receive every reported exception message. */
	TDelegate<void(const std::string&)> OnException;

protected:
	friend TSingleton<FException>;
	FException() = default;
};

} // namespace Exception

} // namespace Maho
