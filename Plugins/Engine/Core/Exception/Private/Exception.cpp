#include "Exception.h"

namespace Maho::Exception
{

FException* GExceptionCenter = nullptr;

MAHO_EXCEPTION_API FException* GetExceptionCenter()
{
	return GExceptionCenter;
}

void FException::Initialize(FEngineBase&)
{
	OnException.RemoveAll();
	GExceptionCenter = this;
}

void FException::Shutdown(FEngineBase&)
{
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
extern "C" MAHO_EXCEPTION_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Exception::FException::CreateLayer();
}
