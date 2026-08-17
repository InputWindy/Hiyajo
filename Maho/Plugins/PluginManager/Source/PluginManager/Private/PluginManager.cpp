#include <PluginManager.h>

#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#	include <Windows.h>
using FModuleHandle = HMODULE;
#	define MAHO_LOAD_LIBRARY(P) LoadLibraryA(P)
#	define MAHO_GET_PROC(H, N) GetProcAddress(H, N)
#	define MAHO_FREE_LIBRARY(H) FreeLibrary(H)
#else
#	include <dlfcn.h>
using FModuleHandle = void*;
#	define MAHO_LOAD_LIBRARY(P) dlopen(P, RTLD_NOW)
#	define MAHO_GET_PROC(H, N) dlsym(H, N)
#	define MAHO_FREE_LIBRARY(H) dlclose(H)
#endif

namespace Maho::PluginManager
{

namespace
{
	struct FLoadedPlugin
	{
		std::string Name;
		FModuleHandle Handle = nullptr;
		IExtension<EEngineStage>* Extension = nullptr;
	};

	std::mutex GMutex;
	std::vector<FLoadedPlugin> GLoaded;
	std::vector<std::string> GInstallRequests;
	std::vector<std::string> GUninstallRequests;

	void FlushInstallRequests()
	{
		for (const std::string& Path : GInstallRequests)
		{
			FModuleHandle Handle = MAHO_LOAD_LIBRARY(Path.c_str());
			if (Handle == nullptr)
			{
				continue;   // TODO: log load failure
			}
			const auto Create = reinterpret_cast<FCreateExtensionFn>(
				MAHO_GET_PROC(Handle, ExtensionFactoryName));
			if (Create == nullptr)
			{
				MAHO_FREE_LIBRARY(Handle);
				continue;   // TODO: log missing factory
			}
			IExtension<EEngineStage>* Extension = Create();
			if (Extension == nullptr)
			{
				MAHO_FREE_LIBRARY(Handle);
				continue;   // TODO: log null extension
			}
			Extension->ExecuteStage(EEngineStage::Init);
			GLoaded.push_back(FLoadedPlugin{ Path, Handle, Extension });
		}
		GInstallRequests.clear();
	}

	void FlushUninstallRequests()
	{
		for (const std::string& Name : GUninstallRequests)
		{
			for (auto It = GLoaded.begin(); It != GLoaded.end(); ++It)
			{
				if (It->Name != Name)
				{
					continue;
				}
				It->Extension->ExecuteStage(EEngineStage::Shutdown);
				delete It->Extension;                       // dtor 在 DLL 里，必须先于卸载
				MAHO_FREE_LIBRARY(It->Handle);
				GLoaded.erase(It);
				break;
			}
		}
		GUninstallRequests.clear();
	}
}

bool FPluginManager::ExecuteStage(EEngineStage Stage)
{
	std::lock_guard<std::mutex> Lock(GMutex);

	// Process queued install/uninstall at the tick boundary — never mid-drive.
	if (Stage == EEngineStage::PreTick)
	{
		FlushInstallRequests();
		FlushUninstallRequests();
	}

	// Forward this stage to every loaded extension (same lifecycle as static ones).
	for (FLoadedPlugin& Plugin : GLoaded)
	{
		Plugin.Extension->ExecuteStage(Stage);
	}

	// Unload everything after the Shutdown stage has been forwarded.
	if (Stage == EEngineStage::Shutdown)
	{
		for (FLoadedPlugin& Plugin : GLoaded)
		{
			delete Plugin.Extension;                       // dtor 在 DLL 里，必须先于卸载
			MAHO_FREE_LIBRARY(Plugin.Handle);
		}
		GLoaded.clear();
		GInstallRequests.clear();
		GUninstallRequests.clear();
	}

	return true;
}

void FPluginManager::Install(std::string_view Path)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	GInstallRequests.emplace_back(Path);
}

void FPluginManager::Uninstall(std::string_view Name)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	GUninstallRequests.emplace_back(Name);
}

} // namespace Maho::PluginManager

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FPluginManagerAdapter final : public Maho::IExtension<Maho::EEngineStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EEngineStage Stage) override
	{
		return Maho::PluginManager::FPluginManager::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_PLUGINMANAGER_API Maho::IExtension<Maho::EEngineStage>* CreateExtension()
{
	return new FPluginManagerAdapter();
}
