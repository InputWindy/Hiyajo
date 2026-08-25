#pragma once

#include <Core/Singleton.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Maho
{
namespace Config
{

/**
 * INI-style configuration singleton — UE DefaultEngine.ini format. Load parses
 * an INI file; GetString/GetInt/... read; SetString overrides at runtime.
 *   FConfig::Get().GetString("/Script/Engine.Engine", "GameName");
 */
class FConfig : public TSingleton<FConfig>
{
public:
	/** Process-unique accessor — declared here, defined in Config.cpp (in Maho.dll). */
	static FConfig& Get();

	void Initiate(int, char**) override { Sections.clear(); }
	void Shutdown() override { Sections.clear(); }

	bool Load(std::string_view Path);
	[[nodiscard]] std::optional<std::string> GetString(std::string_view Section, std::string_view Key) const;
	[[nodiscard]] std::int64_t GetInt(std::string_view Section, std::string_view Key, std::int64_t Default = 0) const;
	[[nodiscard]] double GetFloat(std::string_view Section, std::string_view Key, double Default = 0.0) const;
	[[nodiscard]] bool GetBool(std::string_view Section, std::string_view Key, bool Default = false) const;
	void SetString(std::string_view Section, std::string_view Key, std::string Value);
	[[nodiscard]] bool HasSection(std::string_view Section) const;
	[[nodiscard]] bool HasKey(std::string_view Section, std::string_view Key) const;

private:
	using FSection = std::map<std::string, std::string>;
	std::map<std::string, FSection> Sections;
};

} // namespace Config
} // namespace Maho
