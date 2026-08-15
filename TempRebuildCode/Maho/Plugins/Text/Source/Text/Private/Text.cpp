#include <Text.h>

namespace Maho
{

bool FText::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = setup locale/encoding; Shutdown = nothing.
	(void)Stage;
	return true;
}

} // namespace Maho
