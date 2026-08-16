#include <Network.h>

namespace Maho::Network
{

bool FNetworkSystem::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = start network stack; Tick = pump; Shutdown = stop.
	(void)Stage;
	return true;
}

} // namespace Maho::Network
