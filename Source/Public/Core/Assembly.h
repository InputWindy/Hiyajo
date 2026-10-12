#pragma once

// Assembly -- WHERE MODULES COME FROM: the DLL loading primitive (`FAssembly`'s instance half) plus
// the PLUGIN MANIFEST that says what each node should install (`FAssembly`'s static half). The
// implementation lives in Source/Private/Core/Assembly.cpp, which is also where the platform #if
// branches live (so this header stays platform-agnostic: no Windows.h / dlfcn.h here).

#include <Core/Export.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{

/**
 * Append the host platform's dynamic-library suffix to a module base name.
 *
 * Protocol: a module (frame / engine) always compiles to  <FrameType> + suffix
 * (e.g. FScene.dll / FScene.so / FScene.dylib). The MAHO_DECLARE_* macros bake
 * the base name as #FrameType; this resolves the platform suffix at runtime so
 * no `.dll` is ever hardcoded. iOS has no dynamic library at runtime, so the
 * base name passes through unchanged.
 *
 * Exported: every plugin's generated GetModulePath() calls it from its own DLL.
 */
MAHO_API std::string ApplyModuleExtension(std::string_view BaseName);

/**
 * Custom deleter for the OS module handle -- a DLL is released with
 * FreeLibrary/dlclose, not `delete`. FAssembly owns the handle and releases it
 * on destruction, so the host's module manager just owns FAssembly values.
 *
 * Exported: the deleter runs from whatever module destroys an FAssembly.
 */
struct MAHO_API FModuleDeleter
{
	void operator()(void* Handle) const noexcept;
};

/**
 * A dynamically-loaded code unit -- the OS module handle + symbol lookup.
 *
 * Pure loading primitive: knows nothing about plugins, manifests, or
 * factories. How to interpret a loaded module (which symbols to probe and
 * what they mean) is decided entirely by the consumer -- the plugin manager,
 * a thin launcher, or any project-defined loader.
 *
 * Ownership: this is the single owner of the module handle (unique_ptr ->
 * move-only). The host must keep a loaded FAssembly alive as long as any
 * instance constructed from it lives -- vtables and dtors live in the module,
 * so unloading first is a use-after-free.
 */
class MAHO_API FAssembly
{
public:
	FAssembly() = default;
	explicit FAssembly(std::string_view Path);
	~FAssembly() = default;

	FAssembly(const FAssembly&) = delete;
	FAssembly& operator=(const FAssembly&) = delete;
	FAssembly(FAssembly&&) noexcept = default;
	FAssembly& operator=(FAssembly&&) noexcept = default;

	/** Load the module from a path; false when missing or load fails. */
	bool Load(std::string_view Path);

	/** Unload (FreeLibrary / dlclose); safe to call repeatedly. */
	void Unload();

	[[nodiscard]] bool IsLoaded() const
	{
		return Module != nullptr;
	}

	/** Raw symbol lookup; nullptr when absent or not loaded. */
	[[nodiscard]] void* GetProcAddress(const char* Name) const;

	/** Cast the raw symbol to a typed FUNCTION pointer. */
	template <typename TFunction>
	[[nodiscard]] TFunction GetProcAs(const char* Name) const
	{
		return reinterpret_cast<TFunction>(GetProcAddress(Name));
	}

	/** ── THE PLUGIN MANIFEST (static: one per PROCESS, not one per module) ────────────────────────
	 *
	 * The install tree, as a lookup table: `node -> its direct children`. It is static because it IS
	 * the process's, not a loaded module's: every DLL shares one answer, and a query needs no instance
	 * -- so the caller writes `FAssembly::GetChildren(GetName())`.
	 *
	 * The loader reads `MAHO_PLUGIN_MANIFEST` (a name the GENERATOR fills in, together with the
	 * POST_BUILD step that stages the file next to the binary -- the build layout is not Core's fact,
	 * and `Assembly.cpp` spells no path of its own).
	 *
	 * The manifest is a tree of LAYER TYPES: the project is the ROOT node and every node's children
	 * are the modules IT installs into ITS OWN collector. That is why one query serves everybody --
	 * every node already knows its own name (`GetName()`), so nothing carries a name in the other
	 * direction.
	 *
	 * Deliberately not a manager: nothing here owns a module, installs anything or knows a plugin.
	 * Deciding WHO gets installed is the builder's call, made from what these answer. */
	static bool LoadManifest();

	/** True once a LoadManifest() produced a usable tree. */
	[[nodiscard]] static bool IsManifestLoaded();

	/** DIRECT child layer types of a node, in manifest order -- the single query the host and every
	 *  collector use, each with its own name. Empty (not an error) for a node with none. */
	[[nodiscard]] static const std::vector<std::string>& GetChildren(std::string_view LayerName);

	/** The engine source root recorded by the generator (an absolute path, in the generator's posix
	 *  form). Empty when absent -- callers fall back to their own probing. Never a compile-time
	 *  constant in C++: the manifest is data. */
	[[nodiscard]] static const std::string& GetEngineRoot();

	/** Directory of the running executable (each platform's canonical query); empty on failure. Shared
	 *  by every runtime path probe (manifest lookup, virtual roots). */
	[[nodiscard]] static std::filesystem::path ExecutableDir();

private:
	/** The loaded module handle, or null. */
	std::unique_ptr<void, FModuleDeleter> Module;

	// ── the manifest's state: the three fields the static accessors above read ──
	//
	// Process-wide, and deliberately NOT synchronized against each other: the manifest is loaded ONCE,
	// by the host, before any collector asks (see `LoadManifest`), and only then is it read. A mutex
	// here would pretend to make a re-load mid-flight safe, which is not a state this design has.
	static bool bManifestLoaded;
	static std::string ManifestEngineRoot;
	static std::map<std::string, std::vector<std::string>> ManifestChildren;   // node -> direct children
};

} // namespace Maho
