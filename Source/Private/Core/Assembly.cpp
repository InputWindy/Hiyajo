#include <Core/Assembly.h>

#if defined(_WIN32)
#	include <Windows.h>
#else
#	include <dlfcn.h>
#endif

#include <string>
#include <utility>

namespace Maho
{

std::string ApplyModuleExtension(std::string_view BaseName)
{
#if defined(_WIN32)
	return std::string(BaseName) + ".dll";
#elif defined(__APPLE__)
	return std::string(BaseName) + ".dylib";
#elif defined(__ANDROID__)
	return std::string(BaseName) + ".so";
#elif defined(__linux__)
	return std::string(BaseName) + ".so";
#else
	return std::string(BaseName);
#endif
}

void FModuleDeleter::operator()(void* Handle) const noexcept
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

FAssembly::FAssembly(std::string_view Path)
{
	Load(Path);
}

bool FAssembly::Load(std::string_view Path)
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

void FAssembly::Unload()
{
	Module.reset();
}

void* FAssembly::GetProcAddress(const char* Name) const
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

} // namespace Maho
