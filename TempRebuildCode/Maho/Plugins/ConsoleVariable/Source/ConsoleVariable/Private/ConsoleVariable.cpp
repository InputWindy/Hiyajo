#include <ConsoleVariable.h>

namespace Maho::ConsoleVariable
{

bool FConsoleVariable::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = register builtin cvars; Shutdown = unregister.
	(void)Stage;
	return true;
}

} // namespace Maho::ConsoleVariable
