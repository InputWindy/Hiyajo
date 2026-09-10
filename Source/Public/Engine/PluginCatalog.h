#pragma once

#include <Core/Export.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{

/**
 * Runtime plugin-install manifest, loaded from PluginCatalog.json (staged next to
 * the binary by codegen's EntryPoint POST_BUILD). Answers the two questions the
 * installer needs:
 *   - which layers the host installs up-front (TopLevel);
 *   - the child plugins each parent collector recursively installs into itself
 *     (SubPlugins), resolved by LAYER TYPE (the module base name == GetName()).
 *
 * Process-unique: the accessor is declared here and defined in the engine DLL
 * (Maho), so any plugin DLL linking Maho shares the single instance.
 */
class MAHO_API FPluginCatalog
{
public:
	/** The process-unique instance (lazy-loaded on first access). */
	static FPluginCatalog& Get();

	/** Load (or re-load) PluginCatalog.json. Searches the current working dir and
	 *  the executable's directory. Returns false when absent/unparseable — callers
	 *  fall back. */
	bool Load();

	/** True once a Load() produced a usable catalog. */
	[[nodiscard]] bool IsLoaded() const { return bLoaded; }

	/** Top-level layers the host installs in PreMain, in manifest order. */
	[[nodiscard]] const std::vector<std::string>& GetTopLevel() const { return TopLevel; }

	/** Child layer types a parent collector recursively installs into itself.
	 *  Empty when the parent declares no sub-plugins. */
	[[nodiscard]] std::vector<std::string> GetSubPlugins(std::string_view LayerName) const;

	/** Module base name for a layer (== its layer type under the naming protocol).
	 *  Empty when unknown. */
	[[nodiscard]] std::string GetModule(std::string_view LayerName) const;

private:
	FPluginCatalog() = default;
	~FPluginCatalog() = default;
	FPluginCatalog(const FPluginCatalog&) = delete;
	FPluginCatalog& operator=(const FPluginCatalog&) = delete;

	bool bLoaded = false;
	std::vector<std::string> TopLevel;
	std::map<std::string, std::vector<std::string>> SubPlugins;
	std::map<std::string, std::string> Modules;
};

} // namespace Maho
