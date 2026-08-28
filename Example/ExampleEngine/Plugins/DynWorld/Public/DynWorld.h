#pragma once

#include "DynWorldApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

namespace Maho
{

// DynWorld — an engine feature (one node per stage in the engine
// pipeline: BeginFrame → Tick → EndFrame).
class FDynWorld : public FEngineLayer
{
MAHO_DECLARE_ENGINE_LAYER(FDynWorld, "DynWorld.dll");

public:
	void BeginFrame(FEngineBase& Engine) override;
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase& Engine) override;
};

} // namespace Maho
