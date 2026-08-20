#include "Exception.h"

namespace Maho
{

namespace Exception
{

void FExceptionTool::Clear()
{
	OnException.Clear();
}

void FExceptionTool::ReportException(std::string_view Message)
{
	OnException.Broadcast(std::string(Message));
}

void FExceptionTool::ReportException(const std::exception& Error)
{
	ReportException(Error.what());
}

} // namespace Exception

} // namespace Maho
