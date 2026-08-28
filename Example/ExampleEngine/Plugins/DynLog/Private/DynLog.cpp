#include "DynLog.h"

#include <Log.h>

namespace Maho
{

void FDynLog::BeginFrame() { MAHO_LOG_CORE_INFO("[DynLog] BeginFrame"); }
void FDynLog::Tick()       { MAHO_LOG_CORE_INFO("[DynLog] Tick"); }
void FDynLog::EndFrame()   { MAHO_LOG_CORE_INFO("[DynLog] EndFrame"); }

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_DYNLOG_API Maho::FEngineLayer* CreateLayer()
{
	return Maho::FDynLog::CreateLayer();
}
