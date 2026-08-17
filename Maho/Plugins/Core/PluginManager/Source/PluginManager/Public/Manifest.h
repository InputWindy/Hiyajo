#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Maho
{

namespace PluginManager
{

// ───────────────────────────────────────────────────────────────────────
// .cplugin manifest — C++ runtime mirror.
//
// Faithful mirror of a plugin's .cplugin JSON (string-typed; the loader
// interprets vocabularies like Stage / Priority / Type as needed).
// ───────────────────────────────────────────────────────────────────────

/** Extension declaration — the "type" an assembly provides (name-level; C++ has no reflection). */
struct FManifestExtension
{
	std::string Class;     // "Maho::Log::FLogger"
	std::string Header;    // "Log.h"
	std::string Priority;  // "System" / "Layer" / "Overlay"
	std::string Stage;     // "EEngineStage" / "EToolStage"
};

/** One module in the manifest. */
struct FManifestModule
{
	std::string Name;
	std::string Type;                              // "Runtime" / "Editor"
	std::vector<std::string> Dependencies;
	std::vector<std::string> Inherits;
	std::optional<FManifestExtension> Extension;   // absent = no extension
};

/** The .cplugin manifest — one assembly's metadata. */
struct FManifest
{
	int FileVersion = 1;
	std::string FriendlyName;
	std::string Description;
	std::string Category;
	bool EnabledByDefault = true;
	std::vector<FManifestModule> Modules;
};

} // namespace PluginManager

} // namespace Maho
