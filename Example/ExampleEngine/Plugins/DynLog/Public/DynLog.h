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
MAHO_DECLARE_FEATURE(FDynLog, "DynLog.dll");

public:
	void BeginFrame() override;
	void Tick() override;
	void EndFrame() override;

	// Cross-feature dependency (optional):
	// FDynLog() { AddDependency<ITick, FOther, IBeginFrame>(); }
};

} // namespace Maho
