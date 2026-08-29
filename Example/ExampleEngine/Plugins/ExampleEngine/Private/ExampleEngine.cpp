#include "ExampleEngine.h"

#include <Log.h>

namespace Maho
{
void FExampleEngine::PreMain()
{
	// The engine service layer (Log) and the input driver layer are installed
	// up front; DynLog/DynWorld/DynRender are dynamically installed per-frame
	// by GameInput's Tick.
	Install("Log.dll");
	Install("GameInput.dll");
	Install("Platform.dll");
	Install("Resource.dll");
	Install("Script.dll");
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
