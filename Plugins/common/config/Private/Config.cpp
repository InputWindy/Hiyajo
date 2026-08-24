#include "Config.h"

#include <fstream>
#include <string>
#include <utility>

namespace Maho::Config
{

namespace
{
	[[nodiscard]] std::string Trim(std::string_view S)
	{
		const std::size_t First = S.find_first_not_of(" \t\r");
		if (First == std::string_view::npos)
		{
			return "";
		}
		const std::size_t Last = S.find_last_not_of(" \t\r");
		return std::string(S.substr(First, Last - First + 1));
	}
}

bool FConfig::Load(std::string_view Path)
{
	std::ifstream Stream{ std::string(Path) };
	if (!Stream)
	{
		return false;
	}

	std::string CurrentSection;
	std::string Line;
	while (std::getline(Stream, Line))
	{
		const std::string Trimmed = Trim(Line);
		if (Trimmed.empty() || Trimmed.front() == ';' || Trimmed.front() == '#')
		{
			continue;   // empty / comment
		}

		if (Trimmed.front() == '[' && Trimmed.back() == ']')
		{
			CurrentSection = Trimmed.substr(1, Trimmed.size() - 2);
			continue;
		}

		const std::size_t Eq = Trimmed.find('=');
		if (Eq == std::string::npos)
		{
			continue;   // not a key=value line
		}

		Sections[CurrentSection][Trim(Trimmed.substr(0, Eq))] = Trim(Trimmed.substr(Eq + 1));
	}
	return true;
}

std::optional<std::string> FConfig::GetString(std::string_view Section, std::string_view Key) const
{
	const auto SectionIt = Sections.find(std::string(Section));
	if (SectionIt == Sections.end())
	{
		return std::nullopt;
	}
	const auto KeyIt = SectionIt->second.find(std::string(Key));
	if (KeyIt == SectionIt->second.end())
	{
		return std::nullopt;
	}
	return KeyIt->second;
}

std::int64_t FConfig::GetInt(std::string_view Section, std::string_view Key, std::int64_t Default) const
{
	const auto Value = GetString(Section, Key);
	if (!Value)
	{
		return Default;
	}
	try
	{
		return std::stoll(*Value);
	}
	catch (...)
	{
		return Default;
	}
}

double FConfig::GetFloat(std::string_view Section, std::string_view Key, double Default) const
{
	const auto Value = GetString(Section, Key);
	if (!Value)
	{
		return Default;
	}
	try
	{
		return std::stod(*Value);
	}
	catch (...)
	{
		return Default;
	}
}

bool FConfig::GetBool(std::string_view Section, std::string_view Key, bool Default) const
{
	const auto Value = GetString(Section, Key);
	if (!Value)
	{
		return Default;
	}
	std::string Lower = *Value;
	for (char& C : Lower)
	{
		if (C >= 'A' && C <= 'Z')
		{
			C = static_cast<char>(C - 'A' + 'a');
		}
	}
	return Lower == "true" || Lower == "1" || Lower == "yes" || Lower == "on";
}

void FConfig::SetString(std::string_view Section, std::string_view Key, std::string Value)
{
	Sections[std::string(Section)][std::string(Key)] = std::move(Value);
}

bool FConfig::HasSection(std::string_view Section) const
{
	return Sections.find(std::string(Section)) != Sections.end();
}

bool FConfig::HasKey(std::string_view Section, std::string_view Key) const
{
	return GetString(Section, Key).has_value();
}

} // namespace Maho::Config
