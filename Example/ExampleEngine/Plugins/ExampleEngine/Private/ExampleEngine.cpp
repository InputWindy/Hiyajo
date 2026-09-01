#include "ExampleEngine.h"

#include <Config.h>
#include <Log.h>
#include <Platform.h>
#include <Render.h>
#include <Resource.h>
#include <Script.h>

namespace Maho
{
void FExampleEngine::PreMain()
{
	// Engine service layers installed up front; the window drives the engine
	// loop and FPlatform requests exit when the window is closed.
	Install<FLog>();
	Install<Config::FConfig>();
	Install<Platform::FPlatform>();
	Install<Resource::FResourceSystem>();
	Install<Script::FScriptSystem>();
	Install<FRender>();
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
