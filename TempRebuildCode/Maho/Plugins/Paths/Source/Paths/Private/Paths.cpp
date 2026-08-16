#include <Paths.h>

namespace Maho::Paths
{

bool FPaths::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = resolve project/engine roots; Shutdown = nothing.
	(void)Stage;
	return true;
}

} // namespace Maho::Paths
