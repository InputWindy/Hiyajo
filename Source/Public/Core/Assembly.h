#pragma once

#include <Core/Export.h>

#include <memory>
#include <string>
#include <string_view>

#if defined(_WIN32)
#	include <Windows.h>
#else
#	include <dlfcn.h>
#endif

namespace Maho
{

/**
 * Custom deleter for the OS module handle -- a DLL is released with
 * FreeLibrary/dlclose, not `delete`. FAssembly owns the handle and releases it
 * on destruction, so the host's FModuleManager just owns FAssembly values.
 */
struct FModuleDeleter
{
	void operator()(void* Handle) const noexcept
	{
		if (Handle == nullptr)
		{
			return;
		}
#if defined(_WIN32)
		FreeLibrary(static_cast<HMODULE>(Handle));
#else
		dlclose(Handle);
#endif
	}
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
	explicit FAssembly(std::string_view Path) { Load(Path); }
	~FAssembly() = default;

	FAssembly(const FAssembly&) = delete;
	FAssembly& operator=(const FAssembly&) = delete;
	FAssembly(FAssembly&&) noexcept = default;
	FAssembly& operator=(FAssembly&&) noexcept = default;

	/** Load the module from a path; false when missing or load fails. */
	bool Load(std::string_view Path)
	{
		Unload();
		const std::string Native = std::string(Path);
#if defined(_WIN32)
		Module.reset(static_cast<void*>(LoadLibraryA(Native.c_str())));
#else
		Module.reset(dlopen(Native.c_str(), RTLD_NOW));
#endif
		return IsLoaded();
	}

	/** Unload (FreeLibrary / dlclose); safe to call repeatedly. */
	void Unload() { Module.reset(); }

	[[nodiscard]] bool IsLoaded() const { return Module != nullptr; }

	/** Raw symbol lookup; nullptr when absent or not loaded. */
	[[nodiscard]] void* GetProcAddress(const char* Name) const
	{
		if (Module == nullptr)
		{
			return nullptr;
		}
#if defined(_WIN32)
		return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(Module.get()), Name));
#else
		return dlsym(Module.get(), Name);
#endif
	}

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
