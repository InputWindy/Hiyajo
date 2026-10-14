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
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

namespace
{
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

	struct FManifestState
	{
		bool bLoaded = false;
		std::string EngineRoot;
		std::map<std::string, std::vector<std::string>> Children;
	};

	FManifestState GManifest;

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
}

struct FAssembly::FImpl
{
	std::unique_ptr<void, FModuleDeleter> Module;
};

FAssembly::FAssembly()
	: Impl(std::make_unique<FImpl>())
{
}

FAssembly::FAssembly(std::string_view Path)
	: Impl(std::make_unique<FImpl>())
{
	Load(Path);
}

FAssembly::FAssembly(FAssembly&&) noexcept = default;

FAssembly& FAssembly::operator=(FAssembly&&) noexcept = default;

FAssembly::~FAssembly() = default;

bool FAssembly::Load(std::string_view Path)
{
	Unload();                             // 先卸掉旧模块（返回值这里不关心）
	const std::string Native = std::string(Path);
#if defined(_WIN32)
	Impl->Module.reset(static_cast<void*>(LoadLibraryA(Native.c_str())));
#else
	Impl->Module.reset(dlopen(Native.c_str(), RTLD_NOW));
#endif
	return IsLoaded();
}

bool FAssembly::Unload()
{
	const bool bWasLoaded = Impl->Module != nullptr;
	Impl->Module.reset();
	return bWasLoaded;
}

bool FAssembly::IsLoaded() const
{
	return Impl->Module != nullptr;
}

void* FAssembly::GetProcAddress(const char* Name) const
{
	if (Impl->Module == nullptr)
	{
		return nullptr;
	}
#if defined(_WIN32)
	return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(Impl->Module.get()), Name));
#else
	return dlsym(Impl->Module.get(), Name);
#endif
}

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
	return GManifest.bLoaded;
}

const std::string& FAssembly::GetEngineRoot()
{
	return GManifest.EngineRoot;
}

bool FAssembly::LoadManifest()
{
	const std::optional<std::filesystem::path> Manifest = FindManifestFile();
	if (!Manifest)
	{
		GManifest.bLoaded = false;
		return false;
	}

	std::ifstream In(*Manifest);
	if (!In)
	{
		GManifest.bLoaded = false;
		return false;
	}
	std::ostringstream Buf;
	Buf << In.rdbuf();
	const std::string Text = Buf.str();
	if (Text.empty())
	{
		GManifest.bLoaded = false;
		return false;
	}

	try
	{
		const nlohmann::json J = nlohmann::json::parse(Text);
		GManifest.Children.clear();
		GManifest.EngineRoot.clear();
		GManifest.bLoaded = false;
		if (J.contains("EngineRoot") && J["EngineRoot"].is_string())
		{
			GManifest.EngineRoot = J["EngineRoot"].get<std::string>();
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
					GManifest.Children[It.key()] = std::move(Kids);
				}
			}
		}
		GManifest.bLoaded = true;
		return true;
	}
	catch (const std::exception&)
	{
		GManifest.bLoaded = false;
		return false;
	}
}

const std::vector<std::string>& FAssembly::GetChildren(std::string_view LayerName)
{
	static const std::vector<std::string> Empty;
	const auto It = GManifest.Children.find(std::string(LayerName));
	return It != GManifest.Children.end() ? It->second : Empty;
}

}
