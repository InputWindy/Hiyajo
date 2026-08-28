#include "DynRender.h"

#include <Log.h>

namespace Maho
{

void FDynRender::BeginFrame(FEngineBase&) { MAHO_LOG_CORE_INFO("[DynRender] BeginFrame"); }
void FDynRender::Tick(FEngineBase&)       { MAHO_LOG_CORE_INFO("[DynRender] Tick"); }
void FDynRender::EndFrame(FEngineBase&)   { MAHO_LOG_CORE_INFO("[DynRender] EndFrame (依赖 DynWorld.EndFrame)"); }

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_DYNRENDER_API Maho::FEngineLayer* CreateLayer()
{
	return Maho::FDynRender::CreateLayer();
}
