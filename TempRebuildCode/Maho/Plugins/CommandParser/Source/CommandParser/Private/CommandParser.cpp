#include <CommandParser.h>

namespace Maho::CommandParser
{

bool FCommandParser::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = reset parsed store; Shutdown = nothing.
	(void)Stage;
	return true;
}

void ParseCommandLine(int Argc, char** Argv)
{
	// TODO: parse into FCommandParser::Get() key-value store (idempotent).
	(void)Argc;
	(void)Argv;
}

} // namespace Maho::CommandParser
