#include "ExampleEngine.h"

#include <Log.h>

namespace Maho
{
void FExampleEngine::PreMain()
{
	// Engine service layers installed up front; the window drives the engine
	// loop and FPlatform requests exit when the window is closed.
	Install("Log.dll");
	Install("Platform.dll");
	Install("Resource.dll");
	Install("Script.dll");
	Install("Render.dll");
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
