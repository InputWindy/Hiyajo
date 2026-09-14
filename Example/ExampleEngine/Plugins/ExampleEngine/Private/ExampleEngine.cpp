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
	FEngineBase::PreMain();
	/*
		TODO something
	*/
}

void FExampleEngine::PostMain()
{
	/*
		TODO something
	*/
	FEngineBase::PostMain();
}

} // namespace Maho

// The C export the host (EntryPoint) looks up BY SYMBOL NAME.
extern "C" MAHO_EXAMPLEENGINE_API Maho::FEngineBase* CreateEngine()
{
	return Maho::FExampleEngine::CreateEngine();
}
