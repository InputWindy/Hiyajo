#pragma once

#include <Core/Export.h>

#include <concepts>
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
 * Runnable contract — anything with a Main entry. Compatible with TSingleton
 * (a Renderer can be a singleton AND runnable); it says nothing about how the
 * object is created.
 */
class MAHO_API IRunable
{
public:
	virtual ~IRunable() = default;

	virtual int Main(int Argc, char** Argv) = 0;
};

/**
 * Installable assembly contract — a runnable that is ALSO dynamically
 * installable: its DLL exports `CreateExtension` returning an IAssembly*.
 * Mutually exclusive with TSingleton (an installable app may be instantiated
 * many times; a singleton may not).
 */
class MAHO_API IAssembly : public virtual IRunable
{
public:
	virtual ~IAssembly() = default;
};

// The assembly-export contract: T must provide `static IAssembly* CreateExtension()`.
// codegen emits a static_assert against this so a missing CreateExtension is a
// compile-time error, not a link/load-time surprise.
template <typename T>
concept FAssemblyExport = requires
{
	{ T::CreateExtension() } -> std::convertible_to<IAssembly*>;
};

} // namespace Maho
