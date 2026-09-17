#pragma once

// Assembly -- DLL loading primitive. The implementation lives in
// Source/Private/Core/Assembly.cpp, which is also where the platform #if branches
// live (so this header stays platform-agnostic: no Windows.h / dlfcn.h here).

#include <Core/Export.h>

#include <memory>
#include <string>
#include <string_view>

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

private:
	std::unique_ptr<void, FModuleDeleter> Module;
};

} // namespace Maho
