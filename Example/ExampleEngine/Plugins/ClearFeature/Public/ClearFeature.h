#pragma once

#include "ClearFeatureApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Render.h>

namespace Maho
{

// ClearFeature - a render feature that clears the screen to a solid color.
// Mounts only the IRender stage; FRender drives it inside its render graph.
class FClearFeature : public FLayer<IRender>
{
MAHO_DECLARE_LAYER(FClearFeature, "ClearFeature.dll");

private:
	void Render(FRender& R) override;
};

} // namespace Maho
