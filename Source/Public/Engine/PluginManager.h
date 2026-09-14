#pragma once

#include <Core/Export.h>

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{

/**
 * Runtime plugin-install TREE, loaded from PluginManager.json (staged next to the
 * binary by codegen's EntryPoint POST_BUILD). The manifest is a tree of layer types:
 * the project is the ROOT node, and every node's children are the plugins it installs
 * into its own collector. One query answers it for everybody, and every node already
 * knows its own name -- so host and collector run the SAME call:
 *
 *   InstallChildrenOf(GetName());   // host (MAHO_DECLARE_ENGINE) and layer alike
 *
 * Nothing carries a name in the other direction: the catalog stores the tree and the
 * engine root, not who anybody is.
 *
 * Process-unique: the accessor is declared here and defined in the engine DLL
 * (Maho), so any plugin DLL linking Maho shares the single instance.
 */
class MAHO_API FPluginManager
{
public:
	/** The process-unique instance (lazy-loaded on first access). */
	static FPluginManager& Get();

	/** Load (or re-load) PluginManager.json. Searches the current working dir and
	 *  the executable's directory. Returns false when absent/unparseable — callers
	 *  fall back. */
	bool Load();

	/** True once a Load() produced a usable catalog. */
	[[nodiscard]] bool IsLoaded() const { return bLoaded; }

	/** DIRECT child layer types of a node, in manifest order -- the single query the
	 *  host and every collector use, each with its own name. Empty (not an error) for
	 *  a node with none. */
	[[nodiscard]] const std::vector<std::string>& GetChildren(std::string_view LayerName) const;

	/** Engine source root recorded by codegen (ENGINE_DIR as an absolute path, in
	 *  the generator's posix form). Empty when absent -- callers fall back to their
	 *  own probing. Never a compile-time constant in C++: the manifest is data. */
	[[nodiscard]] const std::string& GetEngineRoot() const { return EngineRoot; }

	/** Directory of the running executable (each platform's canonical query); empty
	 *  on failure. Shared by every runtime path probe (catalog lookup, virtual roots). */
	[[nodiscard]] static std::filesystem::path ExecutableDir();

private:
	FPluginManager() = default;
	~FPluginManager() = default;
	FPluginManager(const FPluginManager&) = delete;
	FPluginManager& operator=(const FPluginManager&) = delete;

	bool bLoaded = false;
	std::string EngineRoot;
	std::map<std::string, std::vector<std::string>> Children;   // node -> its direct children
};

} // namespace Maho
