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
	// Engine layers install from the catalog's install TREE: the project is the root
	// node and the host installs its direct children -- exactly the call a collector
	// layer makes for its own children (InstallChildrenOf(GetName())), one argument
	// apart. Each installed layer then installs ITS children into its own collector,
	// so nothing is installed twice and nobody needs a special "top level" list.
	FPluginCatalog::Get().Load();
	InstallChildrenOf(GetName());
	FlushPendingUpdatePipelines<
		TTypeList<IPreInit, IInit, IPostInit>,
		TTypeList<IPreShutdown, IShutdown, IPostShutdown>
	>();
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
