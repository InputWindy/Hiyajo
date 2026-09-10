#include "ExampleEngine.h"

#include <Config.h>
#include <Log.h>
#include <Name.h>
#include <Paths.h>
#include <Platform.h>
#include <Render.h>
#include <Resource.h>
#include <Script.h>
#include <GameWorld.h>

#include <Engine/PluginCatalog.h>

namespace Maho
{
void FExampleEngine::PreMain()
{
	// Engine layers install from the runtime catalog's TopLevel list (codegen
	// stages it as PluginCatalog.json next to the binary). Each is loaded by
	// module base name, never a hardcoded .dll. Sub-plugins
	// (Render's features, editor components) are installed by their OWN collector,
	// so the host only installs the top levels.
	FPluginCatalog::Get().Load();
	for (const std::string& Layer : FPluginCatalog::Get().GetTopLevel())
	{
		Install(ApplyModuleExtension(Layer));
	}
}

void FExampleEngine::PostMain()
{
}

} // namespace Maho

// The C export the host (EntryPoint) looks up BY SYMBOL NAME.
extern "C" MAHO_EXAMPLEENGINE_API Maho::FEngineBase* CreateEngine()
{
	return Maho::FExampleEngine::CreateEngine();
}
