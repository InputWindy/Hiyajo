#include <Engine/Common/CommandParser.h>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Maho::CommandParser
{

void FCommandParser::Initiate(int Argc, char** Argv)
{
	Parse(Argc, Argv);
}

void FCommandParser::Shutdown()
{
	Clear();
}

void FCommandParser::Parse(int Argc, char** Argv)
{
	// Normalize each "-key" (or "-key=value") to a "--key" long-option so CLI11
	// handles single-dash names without interpreting them as short-flag chains.
	std::vector<std::string> Normalized;
	Normalized.reserve(static_cast<std::size_t>(Argc) + 1);
	Normalized.push_back("maho");   // program name slot CLI11 consumes
	for (int I = 1; I < Argc; ++I)
	{
		const std::string Arg = Argv[I];
		if (Arg.size() < 2 || Arg[0] != '-')
		{
			continue;   // positional/non-hyphen junk — ignore
		}
		if (Arg[1] == '-')
		{
			// already a long option; keep as-is
			Normalized.push_back(Arg);
			continue;
		}

		// single-dash → long-option. Bind the value inline so we never drop it.
		std::string Body = Arg.substr(1);
		if (Body.find('=') != std::string::npos)
		{
			Normalized.push_back("--" + Body);   // --key=value
			continue;
		}

		if (I + 1 < Argc && Argv[I + 1][0] != '-')
		{
			Normalized.push_back("--" + Body + "=" + Argv[I + 1]);   // --key value
			++I;
		}
		else
		{
			Normalized.push_back("--" + Body + "=true");   // bare flag → true
		}
	}

	CLI::App App;
	App.allow_extras();

	// Discover unique option names so we can declare one CLI11 option per key.
	// CLI11 does the real tokenization / quoted-value work.
	std::vector<std::string> Keys;
	for (const std::string& Arg : Normalized)
	{
		if (Arg.size() < 2 || Arg[0] != '-')
		{
			continue;
		}
		std::string Sig = Arg[1] == '-' ? Arg.substr(2) : Arg.substr(1);
		const std::size_t Eq = Sig.find('=');
		if (Eq != std::string::npos)
		{
			Sig = Sig.substr(0, Eq);
		}
		Keys.push_back(Sig);
	}
	std::sort(Keys.begin(), Keys.end());
	Keys.erase(std::unique(Keys.begin(), Keys.end()), Keys.end());

	for (const std::string& Key : Keys)
	{
		// every discovered flag now carries a value (=true for bare flags), so a
		// single expected value binding is deterministic.
		App.add_option("--" + Key)->expected(1);
	}

	try
	{
		App.parse(Normalized);
	}
	catch (const CLI::ParseError&)
	{
		// Don't abort on bad input — read back whatever parsed.
	}

	// Read the parsed results back into the KV store.
	for (const std::string& Key : Keys)
	{
		CLI::Option* Opt = App.get_option_no_throw("--" + Key);
		if (Opt && Opt->count() > 0 && !Opt->results().empty())
		{
			Store[Key] = Opt->results().front();
		}
	}
}

bool FCommandParser::Has(std::string_view Key) const
{
	return Store.find(std::string(Key)) != Store.end();
}

std::string FCommandParser::Get(std::string_view Key) const
{
	const auto It = Store.find(std::string(Key));
	return It != Store.end() ? It->second : std::string();
}

bool FCommandParser::GetBool(std::string_view Key) const
{
	const std::string Lower = Get(Key);
	return Lower == "true" || Lower == "1" || Lower == "yes" || Lower == "on";
}

int FCommandParser::GetInt(std::string_view Key) const
{
	try { return std::stoi(Get(Key)); }
	catch (...) { return 0; }
}

void FCommandParser::Clear()
{
	Store.clear();
}

} // namespace Maho::CommandParser
