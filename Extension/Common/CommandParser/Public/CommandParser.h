#pragma once

#include "CommandParserApi.h"
#include <Maho.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Maho
{

namespace CommandParser
{

/** CommandParser plugin's own drive stage — the host passes it to Execute<Stage>(). */
enum class ECommandParserStage : std::uint8_t
{
	Init = 0,
	Shutdown,
};

/** Command-line argument parser extension (key-value store). */
class MAHO_COMMANDPARSER_API FCommandParser : public Maho::TExtensionList<FCommandParser>
{
public:
	/** Stage dispatch — called by `scheduler.Execute<ECommandParserStage, ...>()`. */
	[[nodiscard]] bool ExecuteStage(ECommandParserStage Stage);

	/**
	 * Get a value from the parsed command line by name. Supports both `-name=value`
	 * and `-name value` forms. Returns nullptr when the key is absent.
	 */
	[[nodiscard]] const std::string* Find(std::string_view Name) const;

	/** True when the key is present (a switch has no value). */
	[[nodiscard]] bool Has(std::string_view Name) const;

	/** Number of parsed entries. */
	[[nodiscard]] int Count() const;

	/** Reset the store (kept idempotent — ParseCommandLine overwrites entries). */
	void Reset();

private:
	friend MAHO_COMMANDPARSER_API void ParseCommandLine(int Argc, char** Argv);

	std::unordered_map<std::string, std::string> Storage;
};

/** Parse argc/argv into FCommandParser's shared store (idempotent; later calls overwrite). */
MAHO_COMMANDPARSER_API void ParseCommandLine(int Argc, char** Argv);

} // namespace CommandParser

} // namespace Maho
