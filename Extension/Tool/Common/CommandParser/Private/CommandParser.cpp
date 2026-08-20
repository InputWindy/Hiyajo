#include "CommandParser.h"

namespace Maho
{

namespace CommandParser
{

void FCommandParserTool::Clear()
{
	Storage.clear();
}

const std::string* FCommandParserTool::Find(std::string_view Name) const
{
	const auto It = Storage.find(std::string(Name));
	return It != Storage.end() ? &It->second : nullptr;
}

bool FCommandParserTool::Has(std::string_view Name) const
{
	return Storage.find(std::string(Name)) != Storage.end();
}

int FCommandParserTool::Count() const
{
	return static_cast<int>(Storage.size());
}

void FCommandParserTool::Reset()
{
	Storage.clear();
}

void FCommandParserTool::Parse(int Argc, char** Argv)
{
	if (Argv == nullptr)
	{
		return;
	}

	Reset();

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
			Storage[Name] = Value;
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
					Storage[Name] = std::string(Next);
					++I;
					continue;
				}
			}
			Storage[Name] = std::string();
		}
	}
}

} // namespace CommandParser

} // namespace Maho
