#include "MeshFeature.h"

namespace Maho
{

// MeshFeature - implementation. Override your mounted stages here.

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_MESHFEATURE_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FMeshFeature::CreateFrame();
}
