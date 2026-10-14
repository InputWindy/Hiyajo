#pragma once

/// @brief Loading a module (DLL) and resolving symbols from it, plus the process-wide plugin manifest.
#include <Core/Export.h>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{

/// @brief Applies the appropriate module extension based on the operating system.
/// @param BaseName The base name of the module.
/// @return The module name with the appropriate extension.
MAHO_API std::string ApplyModuleExtension(std::string_view BaseName);

/// @brief One loaded module: the OS handle, the symbol lookup on it, and the shared plugin manifest.
///
/// The manifest is the `PluginManager.json` the path layer resolves: it is read ONCE per process by
/// `LoadManifest()`, and it is what `GetChildren` answers from -- the plugin tree the engine installs
/// frames along. A module is loaded by its own object and unloaded when that object goes away
/// (`~FAssembly`), so an assembly must outlive every symbol taken out of it.
class MAHO_API FAssembly
{
public:
	/// @brief Constructs an assembly with no module loaded (`IsLoaded` is false until `Load`).
	FAssembly();

	/// @brief Loads the module at `Path` (see `Load`; a failure leaves the object unloaded).
	/// @param Path The path to the module.
	explicit FAssembly(std::string_view Path);

	/// @brief Unloads the module if one is loaded.
	~FAssembly();

	/// @brief Not copyable: the object owns the loaded module's handle.
	FAssembly(const FAssembly&) = delete;
	/// @brief Not copy-assignable: the object owns the loaded module's handle.
	FAssembly& operator=(const FAssembly&) = delete;

	/// @brief Moves the loaded module out of `Other` (which is left unloaded).
	FAssembly(FAssembly&&) noexcept;

	/// @brief Move-assigns: the module previously held is unloaded first.
	FAssembly& operator=(FAssembly&&) noexcept;

	/// @brief Loads the module at `Path`, unloading whatever was loaded before.
	/// @param Path The path to the module (an extension-less name is completed, see
	///             `ApplyModuleExtension`).
	/// @return True if the module was loaded, false otherwise; the object is left unloaded on failure.
	bool Load(std::string_view Path);

	/// @brief Unloads the module.
	/// @return True if a module was unloaded, false if there was nothing loaded.
	bool Unload();

	/// @brief Checks whether a module is loaded.
	/// @return True if a module is loaded, false otherwise.
	[[nodiscard]] bool IsLoaded() const;

	/// @brief Resolves one exported symbol on the loaded module.
	/// @param Name The symbol's name, as exported.
	/// @return The symbol's address, or nullptr when the module is unloaded or has no such export.
	[[nodiscard]] void* GetProcAddress(const char* Name) const;

	/// @brief Resolves one exported symbol and reinterprets it as a function pointer.
	/// @tparam TFunction The function-pointer type to reinterpret the address as.
	/// @param Name The symbol's name, as exported.
	/// @return The function pointer, or nullptr when the symbol is missing.
	template <typename TFunction>
	[[nodiscard]] TFunction GetProcAs(const char* Name) const
	{
		return reinterpret_cast<TFunction>(GetProcAddress(Name));
	}

	/// @brief Reads the plugin manifest (`PluginManager.json`) into the process-wide cache.
	/// @return True if the manifest was read, false otherwise (then the plugin tree is empty and the
	///         engine installs nothing along it).
	static bool LoadManifest();

	/// @brief Checks if the assembly manifest is loaded.
	/// @return True if the manifest is loaded, false otherwise.
	[[nodiscard]] static bool IsManifestLoaded();

	/// @brief Gets the layer's children in the plugin manifest -- the tree the engine installs along.
	/// @param LayerName The name of the layer.
	/// @return A reference to the vector of child node names (empty when the layer is unknown).
	[[nodiscard]] static const std::vector<std::string>& GetChildren(std::string_view LayerName);

	/// @brief Gets the root directory of the engine.
	/// @return A reference to the engine root directory path.
	[[nodiscard]] static const std::string& GetEngineRoot();

	/// @brief Gets the directory of the executable.
	/// @return The directory of the executable.
	[[nodiscard]] static std::filesystem::path ExecutableDir();

private:
	/// @brief The module handle; hidden so <windows.h> stays out of this header.
	struct FImpl;

	/// @brief The hidden state of the loaded module (see FImpl).
	std::unique_ptr<FImpl> Impl;
};

}
