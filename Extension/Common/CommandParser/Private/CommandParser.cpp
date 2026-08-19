#include "CommandParser.h"

namespace Maho
{

namespace CommandParser
{

bool FCommandParser::ExecuteStage(ECommandParserStage Stage)
{
	switch (Stage)
	{
	case ECommandParserStage::Init:
		Storage.clear();
		break;

	case ECommandParserStage::Shutdown:
		Storage.clear();
		break;
	}
	return true;
}

const std::string* FCommandParser::Find(std::string_view Name) const
{
	const auto It = Storage.find(std::string(Name));
	return It != Storage.end() ? &It->second : nullptr;
}

bool FCommandParser::Has(std::string_view Name) const
{
	return Storage.find(std::string(Name)) != Storage.end();
}

int FCommandParser::Count() const
{
	return static_cast<int>(Storage.size());
}

void FCommandParser::Reset()
{
	Storage.clear();
}

void ParseCommandLine(int Argc, char** Argv)
{
	if (Argv == nullptr)
	{
		return;
	}

	FCommandParser& Parser = FCommandParser::Get();
	Parser.Reset();

	for (int I = 1; I < Argc; ++I)
	{
		const char* Arg = Argv[I];
		if (Arg == nullptr)
		{
			continue;
		}
		std::string_view Token(Arg);

		// Skip the leading '-' or '--'.
		if (Token.size() >= 1 && Token.front() == '-')
		{
			Token.remove_prefix(1);
		}
		if (Token.size() >= 1 && Token.front() == '-')
		{
			Token.remove_prefix(1);
		}
		if (Token.empty())
		{
			continue;
		}

		// Split "-name=value" into name / value.
		if (const std::size_t Eq = Token.find('='); Eq != std::string_view::npos)
		{
			const std::string Name(Token.substr(0, Eq));
			const std::string Value(Token.substr(Eq + 1));
			Parser.Storage[Name] = Value;
		}
		else
		{
			// "-name value" (or a bare switch "-name").
			const std::string Name(Token);
			if (I + 1 < Argc && Argv[I + 1] != nullptr)
			{
				const std::string_view Next(Argv[I + 1]);
				const bool NextIsValue = Next.empty() || Next.front() != '-';
				if (NextIsValue)
				{
					Parser.Storage[Name] = std::string(Next);
					++I;
					continue;
				}
			}
			Parser.Storage[Name] = std::string();
		}
	}
}

} // namespace CommandParser

} // namespace Maho
