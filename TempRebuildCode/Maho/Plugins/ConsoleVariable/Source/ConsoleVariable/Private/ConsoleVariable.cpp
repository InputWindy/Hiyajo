#include <ConsoleVariable.h>

namespace Maho::ConsoleVariable
{

bool FConsoleVariable::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = register builtin cvars; Shutdown = unregister.
	(void)Stage;
	return true;
}

} // namespace Maho::ConsoleVariable
