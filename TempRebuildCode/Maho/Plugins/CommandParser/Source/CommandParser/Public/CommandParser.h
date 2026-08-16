#pragma once

#include "CommandParserApi.h"
#include <Engine.h>

namespace Maho
{

/** Command-line argument parser extension (key-value store). Pre-app singleton (driven by ESingletonStage). */
class MAHO_COMMANDPARSER_API FCommandParser final : public TExtension<ESingletonStage, FCommandParser>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FCommandParser>;
	FCommandParser() = default;
};

/** Parse argc/argv into FCommandParser's shared store (idempotent; later calls overwrite). */
MAHO_COMMANDPARSER_API void ParseCommandLine(int Argc, char** Argv);

} // namespace Maho
