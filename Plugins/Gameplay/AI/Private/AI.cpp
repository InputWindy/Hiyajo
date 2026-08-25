#include "AI.h"

namespace Maho
{

// AI — implementation. CreateExtension is inline via
// MAHO_DECLARE_LAYER; add the plugin's per-instance logic here.

} // namespace Maho

// The C export the host (EntryPoint) looks up BY SYMBOL NAME. The inline
// static factory's C++ mangled name is not addressable via GetProcAddress,
// so a plain C bridge is exported (the DLL's single stable entry).
extern "C" MAHO_AI_API Maho::FLayerBase* CreateExtension()
{
	return Maho::FAI::CreateExtension();
}
