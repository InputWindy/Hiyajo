#pragma once

#include "CommandParserApi.h"
#include <Toolkit.h>

namespace Maho
{

namespace CommandParser
{

/** Command-line argument parser extension (key-value store). Pre-app toolkit (driven by EToolStage). */
class MAHO_COMMANDPARSER_API FCommandParser : public TExtension<EToolStage, FCommandParser>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

protected:
	friend TSingleton<FCommandParser>;
	FCommandParser() = default;
};

/** Parse argc/argv into FCommandParser's shared store (idempotent; later calls overwrite). */
MAHO_COMMANDPARSER_API void ParseCommandLine(int Argc, char** Argv);

} // namespace CommandParser

} // namespace Maho
