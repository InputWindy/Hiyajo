#include "ClearFeature.h"

namespace Maho
{

void FClearFeature::Render(FRender& R)
{
	IRHI* RHI = R.GetRHI();
	if (RHI)
	{
		RHI->Clear(0.15f, 0.25f, 0.45f, 1.0f);
	}
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_CLEARFEATURE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FClearFeature::CreateLayer();
}
