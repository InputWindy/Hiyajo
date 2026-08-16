#include <Audio.h>

namespace Maho::Audio
{

bool FAudio::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = open audio device; Shutdown = close.
	(void)Stage;
	return true;
}

} // namespace Maho::Audio
