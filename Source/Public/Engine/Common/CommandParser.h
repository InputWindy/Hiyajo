#pragma once

// CommandParser — command-line parser (engine Common, TSingleton). Parses
// argc/argv into a key-value store. Uses CLI11 (engine third-party, header-only).
// Both --key value and --key=value forms are accepted; positional args are stored
// under positional keys. FCommandParser::Get().Initiate(argc, argv) fills the
// store (idempotent).
#include <Core/Singleton.h>

#include <map>
#include <string>
#include <string_view>

namespace Maho
{
namespace CommandParser
{

/** Command-line argument parser (key-value store). */
class FCommandParser : public TSingleton<FCommandParser>
{
public:
	// Re-expose the static factory — the member Get(string_view) below would
	// otherwise hide TSingleton<FCommandParser>::Get().
	using TSingleton<FCommandParser>::Get;

	void Initiate(int Argc, char** Argv) override;
	void Shutdown() override;

	/** Parse argc/argv into the store (idempotent; later overwrites). */
	void Parse(int Argc, char** Argv);

	/** True when a flag/key is present (whether or not it carries a value). */
	[[nodiscard]] bool Has(std::string_view Key) const;

	/** Value for a key; empty string when absent. */
	[[nodiscard]] std::string Get(std::string_view Key) const;

	/** Value as bool ("true"/"1"/"yes"/"on" → true). */
	[[nodiscard]] bool GetBool(std::string_view Key) const;

	/** Value as int; 0 (or fallback) when absent/unparseable. */
	[[nodiscard]] int GetInt(std::string_view Key) const;

	/** All parsed key→value pairs (const ref). */
	[[nodiscard]] const std::map<std::string, std::string>& GetAll() const { return Store; }

	/** Reset the store. */
	void Clear();

protected:
	friend TSingleton<FCommandParser>;
	FCommandParser() = default;

	std::map<std::string, std::string> Store;
};

} // namespace CommandParser
} // namespace Maho
