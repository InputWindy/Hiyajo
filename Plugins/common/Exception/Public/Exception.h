#pragma once

// Exception ?non-fatal exception broadcast (engine Common, TSingleton).
// ReportException fans out to every OnException subscriber (logging, telemetry,
// crash reporters). Nothing aborts here; fatal errors go through Core/Fatal.
// The multicast event comes from Core/Delegate (Maho::TMulticastEvent).
#include <Core/Delegate.h>
#include <Core/Singleton.h>
#include <Maho.h>

#include <exception>
#include <string>
#include <string_view>

namespace Maho
{
namespace Exception
{

/**
 * Non-fatal exception handling (TSingleton with the fixed lifecycle). Uses:
 *
 *   Exception::FException::Get().OnException.Bind([](const std::string& M) {
 *       Log::Error("exception: {}", M);
 *   });
 *   Exception::FException::Get().ReportException("failed to load texture");
 */
class FException
	: public TSingleton<FException>
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Process-unique accessor ?declared here, defined in Exception.cpp (in Exception.dll). */
	static FException& Get();

	void Initialize(FEngineBase& Engine) override;
	void Shutdown(FEngineBase& Engine) override;

	/** Report a non-fatal exception (broadcasts to OnException). */
	void ReportException(std::string_view Message);

	/** Report from a std::exception (forwards what()). */
	void ReportException(const std::exception& Error);

	/** Subscribers receive every reported exception message. */
	TMulticastEvent<void(const std::string&)> OnException;
protected:
	friend TSingleton<FException>;
	FException() = default;
};

} // namespace Exception
} // namespace Maho
