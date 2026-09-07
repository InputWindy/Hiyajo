#pragma once

#include "ConfigApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

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
 * INI-style configuration engine layer - UE DefaultEngine.ini format. Load parses
 * an INI file; GetString/GetInt/... read; SetString overrides at runtime.
 *   GetConfig()->GetString("/Script/Engine.Engine", "GameName");
 */
class FConfig
	: public FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>
{
	MAHO_DECLARE_LAYER(FConfig, "Config.dll");

private:
	// -- engine layer stages (scheduler-only) --
	void PreInitialize(FEngineBase&) override {}
	void Initialize(FEngineBase&) override;
	void PostInitialize(FEngineBase&) override {}
	void PreShutdown(FEngineBase&) override {}
	void Shutdown(FEngineBase&) override;
	void PostShutdown(FEngineBase&) override {}

public:
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

/** Global accessor - set by Initialize, cleared by Shutdown. */
MAHO_CONFIG_API FConfig* GetConfig();

} // namespace Config
} // namespace Maho
