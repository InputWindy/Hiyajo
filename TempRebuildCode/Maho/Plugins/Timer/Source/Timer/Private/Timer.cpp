#include <Timer.h>

namespace Maho
{

bool FTimer::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = start steady clock; Shutdown = nothing.
	(void)Stage;
	return true;
}

} // namespace Maho
