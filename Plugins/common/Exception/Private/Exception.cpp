#include "Exception.h"

#include <Trace.h>

namespace Maho::Exception
{

FException* GExceptionCenter = nullptr;

MAHO_EXCEPTION_API FException* GetExceptionCenter()
{
	return GExceptionCenter;
}

void FException::Initialize(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IInit, "Exception init", "publish the exception center");
	OnException.RemoveAll();
	GExceptionCenter = this;
}

void FException::Shutdown(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IShutdown, "Exception shutdown", "retract the exception center");
	GExceptionCenter = nullptr;
	OnException.RemoveAll();
}

void FException::ReportException(std::string_view Message)
{
	OnException.Broadcast(std::string(Message));
}

void FException::ReportException(const std::exception& Error)
{
	ReportException(Error.what());
}

} // namespace Maho::Exception

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_EXCEPTION_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::Exception::FException::CreateFrame();
}
