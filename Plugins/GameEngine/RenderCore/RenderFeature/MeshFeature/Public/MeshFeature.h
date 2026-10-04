#pragma once

#include "MeshFeatureApi.h"
#include <Maho.h>
#include <Engine/Frame.h>

namespace Maho
{

// MeshFeature - an engine frame. Add the stage interfaces you implement to the
// IPipeline<...> list, e.g. IPipeline<IInit, IShutdown> or
// IPipeline<IBeginFrame, ITick, IEndFrame, IExit>. Each mounted stage must be
// overridden in this class. A frame is FFrameExtension (identity + the edges it declares)
// plus the ordered stage sequence it runs.
class FMeshFeature : public FFrameExtension, public IPipeline<>
{
MAHO_DECLARE_FRAME(FMeshFeature);
};

} // namespace Maho
