#include <Log.h>

namespace Maho::Log
{

bool FLogger::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = init spdlog sinks; Shutdown = flush + drop.
	(void)Stage;
	return true;
}

} // namespace Maho::Log
