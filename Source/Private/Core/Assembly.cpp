#include <Core/Assembly.h>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#	include <Windows.h>
#else
#	include <dlfcn.h>
#	if defined(__APPLE__)
#		include <cstdint>
#		include <mach-o/dyld.h>
#		include <unistd.h>
#	elif defined(__linux__)
#		include <system_error>
#	endif
#endif

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ── the manifest's runtime location is the GENERATOR's fact ─────────────────────────────────────
//
// `Tools/maho_tools.py` writes the manifest AND stages it next to the binary (POST_BUILD), so the
// generator also knows the path -- and it is the only party that may: a path literal here would be
// Core hardcoding a build layout it does not own. No fallback value on purpose: a translation unit
// without these macros is not built through the generated project.
#ifndef MAHO_PLUGIN_MANIFEST
#	error "MAHO_PLUGIN_MANIFEST is undefined: the plugin manifest's name is filled in by the generator (Tools/maho_tools.py), together with the step that stages the file next to the binary."
#endif
#ifndef MAHO_PLUGIN_MANIFEST_FALLBACK
#	error "MAHO_PLUGIN_MANIFEST_FALLBACK is undefined: the generator emits it as the manifest's path RELATIVE TO THE EXECUTABLE's directory (the un-staged, development location)."
#endif

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

// ── the plugin manifest (FAssembly's static half -- see the header) ─────────────────────────────

namespace
{
	/** The first candidate that actually holds the manifest. Every candidate is built from the
	 *  GENERATOR's macros (see the guard above), so this function spells no path of its own -- only
	 *  the set of places worth probing, which IS a runtime decision: the staged copy beside the binary
	 *  first, then the development (un-staged) location, then the working directory, for a host that
	 *  runs the binary from somewhere else. */
	std::optional<std::filesystem::path> FindManifestFile()
	{
		const std::filesystem::path ExeDir = FAssembly::ExecutableDir();
		std::vector<std::filesystem::path> Candidates;
		if (!ExeDir.empty())
		{
			Candidates.push_back(ExeDir / MAHO_PLUGIN_MANIFEST);
			Candidates.push_back(ExeDir / MAHO_PLUGIN_MANIFEST_FALLBACK);
		}

		std::error_code Ec;
		const std::filesystem::path Cwd = std::filesystem::current_path(Ec);
		if (!Ec)
		{
			Candidates.push_back(Cwd / MAHO_PLUGIN_MANIFEST);
		}

		for (const std::filesystem::path& Candidate : Candidates)
		{
			if (std::filesystem::is_regular_file(Candidate, Ec))
			{
				return Candidate;
			}
		}
		return std::nullopt;
	}

} // namespace

// The manifest's state: process-wide, so it is DEFINED here, in the module that exports FAssembly --
// every plugin DLL then reaches the SAME three fields through the import.
bool FAssembly::bManifestLoaded = false;
std::string FAssembly::ManifestEngineRoot;
std::map<std::string, std::vector<std::string>> FAssembly::ManifestChildren;

std::filesystem::path FAssembly::ExecutableDir()
{
#if defined(_WIN32)
	char Buf[MAX_PATH]{};
	const DWORD N = GetModuleFileNameA(nullptr, Buf, MAX_PATH);
	if (N > 0 && N < MAX_PATH)
	{
		return std::filesystem::path(std::string(Buf, N)).parent_path();
	}
#elif defined(__APPLE__)
	char Buf[PATH_MAX]{};
	std::uint32_t Size = sizeof(Buf);
	if (_NSGetExecutablePath(Buf, &Size) == 0)
	{
		return std::filesystem::path(Buf).parent_path();
	}
#elif defined(__linux__)
	std::error_code Ec;
	const std::filesystem::path P = std::filesystem::read_symlink("/proc/self/exe", Ec);
	if (!Ec)
	{
		return P.parent_path();
	}
#endif
	return {};
}

bool FAssembly::IsManifestLoaded()
{
	return bManifestLoaded;
}

const std::string& FAssembly::GetEngineRoot()
{
	return ManifestEngineRoot;
}

bool FAssembly::LoadManifest()
{
	const std::optional<std::filesystem::path> Manifest = FindManifestFile();
	if (!Manifest)
	{
		bManifestLoaded = false;
		return false;
	}

	std::ifstream In(*Manifest);
	if (!In)
	{
		bManifestLoaded = false;
		return false;
	}
	std::ostringstream Buf;
	Buf << In.rdbuf();
	const std::string Text = Buf.str();
	if (Text.empty())
	{
		bManifestLoaded = false;
		return false;
	}

	try
	{
		const nlohmann::json J = nlohmann::json::parse(Text);
		ManifestChildren.clear();
		ManifestEngineRoot.clear();
		// Failing at any point clears the engine root too -- a stale root from a previous successful
		// load must not survive a failed re-load.
		bManifestLoaded = false;
		if (J.contains("EngineRoot") && J["EngineRoot"].is_string())
		{
			ManifestEngineRoot = J["EngineRoot"].get<std::string>();
		}
		if (J.contains("Children") && J["Children"].is_object())
		{
			for (auto It = J["Children"].begin(); It != J["Children"].end(); ++It)
			{
				if (!It.value().is_array())
				{
					continue;
				}
				std::vector<std::string> Kids;
				for (const auto& Child : It.value())
				{
					if (Child.is_string())
					{
						Kids.push_back(Child.get<std::string>());
					}
				}
				if (!Kids.empty())
				{
					ManifestChildren[It.key()] = std::move(Kids);
				}
			}
		}
		bManifestLoaded = true;
		return true;
	}
	catch (const std::exception&)
	{
		bManifestLoaded = false;
		return false;
	}
}

const std::vector<std::string>& FAssembly::GetChildren(std::string_view LayerName)
{
	static const std::vector<std::string> Empty;
	const auto It = ManifestChildren.find(std::string(LayerName));
	return It != ManifestChildren.end() ? It->second : Empty;
}

} // namespace Maho
