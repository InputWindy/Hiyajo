#include <Config.h>

namespace Maho
{

bool FConfig::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = load config; Shutdown = save if dirty.
	(void)Stage;
	return true;
}

} // namespace Maho
