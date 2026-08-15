#include <Physics.h>

namespace Maho
{

bool FPhysics::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = create solver world; Shutdown = destroy.
	(void)Stage;
	return true;
}

} // namespace Maho
