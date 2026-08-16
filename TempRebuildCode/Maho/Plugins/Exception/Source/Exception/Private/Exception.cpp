#include <Exception.h>

namespace Maho::Exception
{

bool FException::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = register exception handlers; Shutdown = unregister.
	(void)Stage;
	return true;
}

} // namespace Maho::Exception
