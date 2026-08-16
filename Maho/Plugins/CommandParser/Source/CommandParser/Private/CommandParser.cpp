#include <CommandParser.h>

namespace Maho::CommandParser
{

bool FCommandParser::ExecuteStage(EToolStage Stage)
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

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FCommandParserAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::CommandParser::FCommandParser::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_COMMANDPARSER_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FCommandParserAdapter();
}
