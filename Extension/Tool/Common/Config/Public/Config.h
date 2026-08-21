#pragma once

#include "ConfigApi.h"
#include <Maho.h>
#include <Engine/Tool.h>

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
 * INI-style configuration — UE DefaultEngine.ini format (sections + key=value).
 *
 *   [/Script/Engine.Engine]
 *   GameName=MyGame
 *   ; comment
 *
 *   [/Script/EngineSettings.GameMapsSettings]
 *   EditorStartupMap=/Game/Maps/Startup
 *
 *   FConfigTool::Get().GetString("/Script/Engine.Engine", "GameName");
 */
class MAHO_CONFIG_API FConfigTool : public Maho::TTool<FConfigTool>
{
public:
	/** Raw string lookup by section + key; nullopt when absent. */
	[[nodiscard]] std::optional<std::string> GetString(std::string_view Section, std::string_view Key) const;

	/** Typed helpers (parse from the raw string; fall back to Default). */
	[[nodiscard]] std::int64_t GetInt(std::string_view Section, std::string_view Key, std::int64_t Default = 0) const;
	[[nodiscard]] double GetFloat(std::string_view Section, std::string_view Key, double Default = 0.0) const;
	[[nodiscard]] bool GetBool(std::string_view Section, std::string_view Key, bool Default = false) const;

	[[nodiscard]] bool HasSection(std::string_view Section) const;
	[[nodiscard]] bool HasKey(std::string_view Section, std::string_view Key) const;

	/** Parse an INI file; returns false on failure. */
	bool Load(std::string_view Path);

	/** Set a value (runtime override). */
	void SetString(std::string_view Section, std::string_view Key, std::string Value);

	/** Drop all loaded sections. */
	void Clear();

private:
	using FSection = std::map<std::string, std::string>;   // key → value
	std::map<std::string, FSection> Sections;              // section → entries
};

} // namespace Config

} // namespace Maho

