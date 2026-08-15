#include <Exception.h>

namespace Maho
{

bool FException::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = register exception handlers; Shutdown = unregister.
	(void)Stage;
	return true;
}

} // namespace Maho
