#include <Engine/PluginCatalog.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#	include <windows.h>
#elif defined(__APPLE__)
#	include <cstdint>
#	include <mach-o/dyld.h>
#	include <unistd.h>
#elif defined(__linux__)
#	include <system_error>
#endif

#include <optional>

namespace Maho
{

namespace
{

/** First directory (of the candidates) that actually holds PluginCatalog.json. */
std::optional<std::filesystem::path> FindCatalogFile()
{
	const std::filesystem::path ExeDir = FPluginCatalog::ExecutableDir();
	const std::filesystem::path Cwd = std::filesystem::current_path();
	std::vector<std::filesystem::path> Candidates;
	if (!ExeDir.empty())
	{
		Candidates.push_back(ExeDir / "PluginCatalog.json");
		Candidates.push_back(ExeDir / ".." / "Intermediate" / "PluginCatalog.json");
	}
	Candidates.push_back(Cwd / "PluginCatalog.json");
	for (const auto& C : Candidates)
	{
		std::error_code Ec;
		if (std::filesystem::is_regular_file(C, Ec))
		{
			return C;
		}
	}
	return std::nullopt;
}

} // namespace

std::filesystem::path FPluginCatalog::ExecutableDir()
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
	const std::uint32_t Size = sizeof(Buf);
	if (_NSGetExecutablePath(Buf, &Size) == 0)
	{
		return std::filesystem::path(Buf).parent_path();
	}
#elif defined(__linux__)
	std::error_code Ec;
	std::filesystem::path P = std::filesystem::read_symlink("/proc/self/exe", Ec);
	if (!Ec)
	{
		return P.parent_path();
	}
#endif
	return {};
}

FPluginCatalog& FPluginCatalog::Get()
{
	static FPluginCatalog Instance;
	return Instance;
}

bool FPluginCatalog::Load()
{
	const std::optional<std::filesystem::path> Catalog = FindCatalogFile();
	if (!Catalog)
	{
		bLoaded = false;
		return false;
	}

	std::ifstream In(*Catalog);
	if (!In)
	{
		bLoaded = false;
		return false;
	}
	std::ostringstream Buf;
	Buf << In.rdbuf();
	const std::string Text = Buf.str();
	if (Text.empty())
	{
		bLoaded = false;
		return false;
	}

	try
	{
		const auto J = nlohmann::json::parse(Text);
		TopLevel.clear();
		SubPlugins.clear();
		Modules.clear();
		EngineRoot.clear();
		// Load() failing at any point clears the engine root too -- a stale root from
		// a previous successful Load must not survive a failed re-load.
		bLoaded = false;
		if (J.contains("EngineRoot") && J["EngineRoot"].is_string())
		{
			EngineRoot = J["EngineRoot"].get<std::string>();
		}
		if (J.contains("TopLevel") && J["TopLevel"].is_array())
		{
			for (const auto& E : J["TopLevel"])
			{
				if (E.is_string())
				{
					TopLevel.push_back(E.get<std::string>());
				}
			}
		}
		if (J.contains("ByLayerName") && J["ByLayerName"].is_object())
		{
			for (auto It = J["ByLayerName"].begin(); It != J["ByLayerName"].end(); ++It)
			{
				const std::string Name = It.key();
				const auto& V = It.value();
				std::vector<std::string> Subs;
				if (V.contains("SubPlugins") && V["SubPlugins"].is_array())
				{
					for (const auto& S : V["SubPlugins"])
					{
						if (S.is_string())
						{
							Subs.push_back(S.get<std::string>());
						}
					}
				}
				SubPlugins[Name] = std::move(Subs);
				std::string Module = Name;
				if (V.contains("Module") && V["Module"].is_string())
				{
					Module = V["Module"].get<std::string>();
				}
				Modules[Name] = std::move(Module);
			}
		}
		bLoaded = true;
		return true;
	}
	catch (const std::exception&)
	{
		bLoaded = false;
		return false;
	}
}

std::vector<std::string> FPluginCatalog::GetSubPlugins(std::string_view LayerName) const
{
	const auto It = SubPlugins.find(std::string(LayerName));
	return It != SubPlugins.end() ? It->second : std::vector<std::string>{};
}

std::string FPluginCatalog::GetModule(std::string_view LayerName) const
{
	const auto It = Modules.find(std::string(LayerName));
	return It != Modules.end() ? It->second : std::string{};
}

} // namespace Maho
