#include <Archive.h>

namespace Maho
{

bool FArchive::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = nothing; Shutdown = nothing.
	(void)Stage;
	return true;
}

} // namespace Maho
