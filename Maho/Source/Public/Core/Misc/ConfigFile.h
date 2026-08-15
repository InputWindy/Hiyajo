#pragma once

#include <Core/Misc/Export.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

/**
 * Minimal UE-style .ini reader (sections / key=value).
 * Comments: lines starting with ';' or '#'. Supports UTF-8 BOM.
 *
 * Example:
 * ```
 *   Maho::FConfigFile Ini;
 *   if (Ini.Load("Config/DefaultEngine.ini"))
 *   {
 *       int Width = 0;
 *       Ini.TryGetInt("SystemSettings", "WindowWidth", Width);
 *       for (const std::string& Key : Ini.GetKeys("ConsoleVariables"))
 *       {
 *           std::string Value;
 *           Ini.TryGetString("ConsoleVariables", Key, Value);
 *       }
 *   }
 * ```
 */
class MAHO_API FConfigFile
{
public:
	FConfigFile() = default;

	/** Load (replace) contents from disk. Returns false if the file cannot be opened. */
	[[nodiscard]] bool Load(const std::string& FilePath);

	[[nodiscard]] bool HasSection(const std::string& Section) const;

	[[nodiscard]] bool TryGetString(const std::string& Section, const std::string& Key, std::string& OutValue) const;
	[[nodiscard]] bool TryGetInt(const std::string& Section, const std::string& Key, int& OutValue) const;
	[[nodiscard]] bool TryGetFloat(const std::string& Section, const std::string& Key, float& OutValue) const;
	[[nodiscard]] bool TryGetBool(const std::string& Section, const std::string& Key, bool& OutValue) const;

	/** Keys in Section (empty if missing). Order is insertion order within the file. */
	[[nodiscard]] std::vector<std::string> GetKeys(const std::string& Section) const;

	[[nodiscard]] const std::string& GetSourcePath() const
	{
		return SourcePath;
	}

private:
	using FSectionMap = std::unordered_map<std::string, std::string>;
	std::unordered_map<std::string, FSectionMap> Sections;
	std::unordered_map<std::string, std::vector<std::string>> SectionKeyOrder;
	std::string SourcePath;
};

} // namespace Maho
