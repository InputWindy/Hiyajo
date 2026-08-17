#pragma once

#include "PluginManagerApi.h"
#include <Engine.h>

#include <string_view>

namespace Maho
{

namespace PluginManager
{

/** Factory name every dynamic plugin DLL must export (extern "C"). */
inline constexpr const char* ExtensionFactoryName = "CreateExtension";

/** Factory signature: returns a heap-allocated IExtension<EEngineStage> (owned by the manager). */
using FCreateExtensionFn = IExtension<EEngineStage>* (*)();

/**
 * Runtime plugin manager (engine extension, driven by EEngineStage).
 *
 *   FPluginManager::Get().Install("GameEngine.dll");    // queue (UI thread)
 *   FPluginManager::Get().Uninstall("GameEngine");      // queue
 *
 * Install/Uninstall only queue requests; the actual LoadLibrary/Init and
 * Shutdown/FreeLibrary run inside ExecuteStage, so the loaded list is never
 * mutated while it is being driven.
 *
 * The manager drives every loaded extension through the SAME stage enum as
 * static extensions: ExecuteStage(EEngineStage::PreInit/Init/Tick/...).
 */
class MAHO_PLUGINMANAGER_API FPluginManager : public TExtension<EEngineStage, FPluginManager>
{
public:
	[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

	/** Queue an install request (LoadLibrary + Init at the next stage boundary). */
	void Install(std::string_view Path);

	/** Queue an uninstall request (Shutdown + FreeLibrary at the next stage boundary). */
	void Uninstall(std::string_view Name);

protected:
	friend TSingleton<FPluginManager>;
	FPluginManager() = default;
};

} // namespace PluginManager

} // namespace Maho
