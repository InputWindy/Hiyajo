#pragma once

#include <Core/Export.h>

#include <string_view>

namespace Maho
{

/**
 * A dynamically-loaded code unit — the OS module handle + symbol lookup.
 *
 * Pure loading primitive: knows nothing about plugins, manifests, or
 * factories. How to interpret a loaded module (which symbols to probe and
 * what they mean) is decided entirely by the consumer — the plugin manager,
 * a thin launcher, or any project-defined loader.
 */
class MAHO_API FAssembly
{
public:
	FAssembly() = default;
	explicit FAssembly(std::string_view Path);
	~FAssembly();

	FAssembly(const FAssembly&) = delete;
	FAssembly& operator=(const FAssembly&) = delete;
	FAssembly(FAssembly&& Other) noexcept;
	FAssembly& operator=(FAssembly&& Other) noexcept;

	/** Load the module from a path; false when missing or load fails. */
	bool Load(std::string_view Path);

	/** Unload (FreeLibrary / dlclose); safe to call repeatedly. */
	void Unload();

	[[nodiscard]] bool IsLoaded() const { return Handle != nullptr; }

	/** Raw symbol lookup; nullptr when absent or not loaded. */
	[[nodiscard]] void* GetProcAddress(const char* Name) const;

	/** Typed symbol lookup. */
	template <typename T>
	[[nodiscard]] T* GetProc(const char* Name) const
	{
		return reinterpret_cast<T*>(GetProcAddress(Name));
	}

private:
	void* Handle = nullptr;
};

/**
 * Loaded assembly contract — an installed extension with a Main entry.
 *
 * The extension DLL's CreateExtension returns this; the entry point calls
 * Main(Argc, Argv) and owns the instance (deletes it after). No stage, no
 * runnable preset — the assembly decides its whole shape.
 */
class MAHO_API IAssembly
{
public:
	virtual ~IAssembly() = default;

	virtual int Main(int Argc, char** Argv) = 0;
};

} // namespace Maho
