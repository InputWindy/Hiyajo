#include "MeshRenderSystem.h"

namespace Maho
{

// MeshRenderSystem - implementation. Override your mounted stages here.

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_MESHRENDERSYSTEM_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FMeshRenderSystem::CreateFrame();
}
