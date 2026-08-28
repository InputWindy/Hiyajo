#pragma once

#include "DynLogApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

namespace Maho
{

// DynLog — an engine feature (one node per stage in the engine
// pipeline: BeginFrame → Tick → EndFrame).
class FDynLog : public FEngineLayer
{
MAHO_DECLARE_LAYER(FDynLog);
MAHO_DECLARE_ENGINE_LAYER(FDynLog, "DynLog.dll");

public:
	void BeginFrame(FEngineBase& Engine) override;
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase& Engine) override;
};

} // namespace Maho
