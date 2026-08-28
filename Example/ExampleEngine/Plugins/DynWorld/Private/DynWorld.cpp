#include "DynWorld.h"

#include <Log.h>

namespace Maho
{

void FDynWorld::BeginFrame(FEngineBase&) { MAHO_LOG_CORE_INFO("[DynWorld] BeginFrame"); }
void FDynWorld::Tick(FEngineBase&)       { MAHO_LOG_CORE_INFO("[DynWorld] Tick (依赖 DynLog.EndFrame)"); }
void FDynWorld::EndFrame(FEngineBase&)   { MAHO_LOG_CORE_INFO("[DynWorld] EndFrame"); }

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_DYNWORLD_API Maho::FEngineLayer* CreateLayer()
{
	return Maho::FDynWorld::CreateLayer();
}
