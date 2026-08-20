#pragma once

#include "ExceptionApi.h"
#include <Maho.h>
#include <Engine/Tool.h>

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace Maho
{

namespace Exception
{

/**
 * Non-fatal exception handling extension.
 *
 * ReportException broadcasts the message to every subscriber — logging,
 * telemetry, crash reporters hook OnException. Nothing aborts here; fatal
 * errors go through Core/Fatal instead.
 *
 *   FExceptionTool::Get().OnException.Add([](const std::string& M) {
 *       Maho::Log::Error("exception: {}", M);
 *   });
 *   FExceptionTool::Get().ReportException("failed to load texture");
 */
class MAHO_EXCEPTION_API FExceptionTool : public Maho::TTool<FExceptionTool>
{
public:
	/** Identity tag — this is a Tool. */
	using FTags = TTypeList<FToolTag>;

	/** Subscribers receive every reported exception message. */
	Maho::TMulticastDelegate<void(const std::string&)> OnException;

protected:
	/** Report a non-fatal exception (broadcasts to OnException). */
	void ReportException(std::string_view Message);

	/** Report from a std::exception (forwards what()). */
	void ReportException(const std::exception& Error);

	/** Lifecycle write (Init/Shutdown): drop all exception subscribers. */
	void Clear();

	template <typename TExtension, typename TStage>
	friend bool Maho::ExecuteExtension(TStage Stage);
};

} // namespace Exception

} // namespace Maho
