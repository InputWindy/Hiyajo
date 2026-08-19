#include "Exception.h"

namespace Maho
{

namespace Exception
{

bool FException::ExecuteStage(EExceptionStage Stage)
{
	switch (Stage)
	{
	case EExceptionStage::Init:
	case EExceptionStage::Shutdown:
		OnException.Clear();
		break;
	}
	return true;
}

void FException::ReportException(std::string_view Message)
{
	OnException.Broadcast(std::string(Message));
}

void FException::ReportException(const std::exception& Error)
{
	ReportException(Error.what());
}

} // namespace Exception

} // namespace Maho
