#pragma once

#include "CommandParserApi.h"
#include <Maho.h>
#include <Engine/Tool.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Maho
{

namespace CommandParser
{

/** Command-line argument parser extension (key-value store). */
class MAHO_COMMANDPARSER_API FCommandParserTool : public Maho::TTool<FCommandParserTool>
{
public:
	/**
	 * Get a value from the parsed command line by name. Supports both `-name=value`
	 * and `-name value` forms. Returns nullptr when the key is absent.
	 */
	[[nodiscard]] const std::string* Find(std::string_view Name) const;

	/** True when the key is present (a switch has no value). */
	[[nodiscard]] bool Has(std::string_view Name) const;

	/** Number of parsed entries. */
	[[nodiscard]] int Count() const;

	/** Reset the store (kept idempotent — Parse overwrites entries). */
	void Reset();

	/** Parse argc/argv into the shared store (idempotent; later calls overwrite). */
	void Parse(int Argc, char** Argv);

	/** Drop all parsed entries. */
	void Clear();

private:
	std::unordered_map<std::string, std::string> Storage;
};

} // namespace CommandParser

} // namespace Maho
