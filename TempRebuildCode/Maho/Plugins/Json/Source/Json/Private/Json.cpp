#include <Json.h>

namespace Maho::Json
{

bool FJson::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = nothing (header-only); Shutdown = nothing.
	(void)Stage;
	return true;
}

} // namespace Maho::Json
