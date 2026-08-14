#include <Core/System/ConfigFile.h>
#include <Core/System/Utf8Path.h>

#include <cctype>
#include <fstream>

namespace Maho
{

namespace
{

std::string Trim(std::string Text)
{
	std::size_t Begin = 0;
	while (Begin < Text.size() && std::isspace(static_cast<unsigned char>(Text[Begin])))
	{
		++Begin;
	}

	std::size_t End = Text.size();
	while (End > Begin && std::isspace(static_cast<unsigned char>(Text[End - 1])))
	{
		--End;
	}

	return Text.substr(Begin, End - Begin);
}

std::string ToLowerAscii(std::string Text)
{
	for (char& Ch : Text)
	{
		Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
	}
	return Text;
}

bool ParseBoolToken(const std::string& Text, bool& OutValue)
{
	const std::string Lower = ToLowerAscii(Text);
	if (Lower == "1" || Lower == "true" || Lower == "yes" || Lower == "on")
	{
		OutValue = true;
		return true;
	}
	if (Lower == "0" || Lower == "false" || Lower == "no" || Lower == "off")
	{
		OutValue = false;
		return true;
	}
	return false;
}

} // namespace

bool FConfigFile::Load(const std::string& FilePath)
{
	Sections.clear();
	SectionKeyOrder.clear();
	SourcePath = FilePath;

	std::ifstream Input(PathFromUtf8(FilePath));
	if (!Input)
	{
		return false;
	}

	std::string CurrentSection;
	std::string Line;
	bool bFirstLine = true;

	while (std::getline(Input, Line))
	{
		if (!Line.empty() && Line.back() == '\r')
		{
			Line.pop_back();
		}

		if (bFirstLine)
		{
			bFirstLine = false;
			// UTF-8 BOM
			if (Line.size() >= 3
				&& static_cast<unsigned char>(Line[0]) == 0xEF
				&& static_cast<unsigned char>(Line[1]) == 0xBB
				&& static_cast<unsigned char>(Line[2]) == 0xBF)
			{
				Line.erase(0, 3);
			}
		}

		Line = Trim(Line);
		if (Line.empty() || Line[0] == ';' || Line[0] == '#')
		{
			continue;
		}

		if (Line.front() == '[' && Line.back() == ']')
		{
			CurrentSection = Trim(Line.substr(1, Line.size() - 2));
			continue;
		}

		if (CurrentSection.empty())
		{
			continue;
		}

		const std::size_t Eq = Line.find('=');
		if (Eq == std::string::npos)
		{
			continue;
		}

		const std::string Key = Trim(Line.substr(0, Eq));
		const std::string Value = Trim(Line.substr(Eq + 1));
		if (Key.empty())
		{
			continue;
		}

		FSectionMap& Section = Sections[CurrentSection];
		const bool bNewKey = Section.find(Key) == Section.end();
		Section[Key] = Value;
		if (bNewKey)
		{
			SectionKeyOrder[CurrentSection].push_back(Key);
		}
	}

	return true;
}

bool FConfigFile::HasSection(const std::string& Section) const
{
	return Sections.find(Section) != Sections.end();
}

bool FConfigFile::TryGetString(const std::string& Section, const std::string& Key, std::string& OutValue) const
{
	const auto SectionIt = Sections.find(Section);
	if (SectionIt == Sections.end())
	{
		return false;
	}

	const auto KeyIt = SectionIt->second.find(Key);
	if (KeyIt == SectionIt->second.end())
	{
		return false;
	}

	OutValue = KeyIt->second;
	return true;
}

bool FConfigFile::TryGetInt(const std::string& Section, const std::string& Key, int& OutValue) const
{
	std::string Text;
	if (!TryGetString(Section, Key, Text))
	{
		return false;
	}

	try
	{
		OutValue = std::stoi(Text);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool FConfigFile::TryGetFloat(const std::string& Section, const std::string& Key, float& OutValue) const
{
	std::string Text;
	if (!TryGetString(Section, Key, Text))
	{
		return false;
	}

	try
	{
		OutValue = std::stof(Text);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool FConfigFile::TryGetBool(const std::string& Section, const std::string& Key, bool& OutValue) const
{
	std::string Text;
	if (!TryGetString(Section, Key, Text))
	{
		return false;
	}
	return ParseBoolToken(Text, OutValue);
}

std::vector<std::string> FConfigFile::GetKeys(const std::string& Section) const
{
	const auto It = SectionKeyOrder.find(Section);
	if (It == SectionKeyOrder.end())
	{
		return {};
	}
	return It->second;
}

} // namespace Maho
