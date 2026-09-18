#pragma once

// Exception - non-fatal exception broadcast (engine Common, FEngineLayer).
// ReportException fans out to every OnException subscriber (logging, telemetry,
// crash reporters). Nothing aborts here; fatal errors go through Core/Fatal.
// The multicast event comes from Core/Delegate (Maho::TMulticastEvent).
#include <Core/Delegate.h>
#include <Maho.h>
#include <Engine/Engine.h>

#include "ExceptionApi.h"

#include <exception>
#include <string>
#include <string_view>

namespace Maho
{
namespace Exception
{

class FException;

/** Global exception center accessor - returns FException* (cross-DLL via function). */
MAHO_EXCEPTION_API FException* GetExceptionCenter();

/**
 * Non-fatal exception handling (an engine layer mounting Init + Shutdown). Uses:
 *
 *   Exception::GetExceptionCenter()->OnException.Bind([](const std::string& M) {
 *       Log::Error("exception: {}", M);
 *   });
 *   Exception::GetExceptionCenter()->ReportException("failed to load texture");
 */
class FException : public FFrameExtension, public IPipeline<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>
{
public:
	MAHO_DECLARE_FRAME(FException);

	/** Report a non-fatal exception (broadcasts to OnException). */
	void ReportException(std::string_view Message);

	/** Report from a std::exception (forwards what()). */
	void ReportException(const std::exception& Error);

	/** Subscribers receive every reported exception message. */
	TMulticastEvent<void(const std::string&)> OnException;

private:
	// -- engine pipeline stages (scheduler-only) --
	void PreInitialize(FEngineBase&, FEngineContext&) override {}
	void Initialize(FEngineBase& Engine, FEngineContext& Frame) override;
	void PostInitialize(FEngineBase&, FEngineContext&) override {}
	void PreShutdown(FEngineBase&, FEngineContext&) override {}
	void Shutdown(FEngineBase& Engine, FEngineContext& Frame) override;
	void PostShutdown(FEngineBase&, FEngineContext&) override {}
};

} // namespace Exception
} // namespace Maho
