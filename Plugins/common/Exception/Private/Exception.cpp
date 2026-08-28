#include "Exception.h"

namespace Maho::Exception
{

FException& FException::Get()
{
	static FException Instance;
	return Instance;
}

void FException::Initialize(FEngineBase& Engine)
{
	(void)Engine;
	OnException.RemoveAll();
}

void FException::Shutdown(FEngineBase&)
{
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
