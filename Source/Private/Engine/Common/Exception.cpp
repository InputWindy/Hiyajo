#include <Engine/Common/Exception.h>

namespace Maho::Exception
{

FException& FException::Get()
{
	static FException Instance;
	return Instance;
}

void FException::Initialize(int Argc, char** Argv)
{
	(void)Argc; (void)Argv;
	OnException.RemoveAll();
}

void FException::Shutdown()
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
