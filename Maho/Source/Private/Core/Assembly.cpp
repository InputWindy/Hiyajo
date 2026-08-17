#include <Core/Assembly.h>

#include <string>
#include <utility>

#if defined(_WIN32)
#	include <Windows.h>
#else
#	include <dlfcn.h>
#endif

namespace Maho
{

FAssembly::FAssembly(std::string_view Path)
{
	Load(Path);
}

FAssembly::~FAssembly()
{
	Unload();
}

FAssembly::FAssembly(FAssembly&& Other) noexcept
	: Handle(Other.Handle)
{
	Other.Handle = nullptr;
}

FAssembly& FAssembly::operator=(FAssembly&& Other) noexcept
{
	if (this != &Other)
	{
		Unload();
		Handle = Other.Handle;
		Other.Handle = nullptr;
	}
	return *this;
}

bool FAssembly::Load(std::string_view Path)
{
	Unload();
	const std::string Native = std::string(Path);
#if defined(_WIN32)
	Handle = static_cast<void*>(LoadLibraryA(Native.c_str()));
#else
	Handle = dlopen(Native.c_str(), RTLD_NOW);
#endif
	return Handle != nullptr;
}

void FAssembly::Unload()
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
	Handle = nullptr;
}

void* FAssembly::GetProcAddress(const char* Name) const
{
	if (Handle == nullptr)
	{
		return nullptr;
	}
#if defined(_WIN32)
	return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(Handle), Name));
#else
	return dlsym(Handle, Name);
#endif
}

} // namespace Maho
