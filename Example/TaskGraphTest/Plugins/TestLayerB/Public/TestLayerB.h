#pragma once

#include "TestLayerBApi.h"
#include <Engine/Engine.h>
#include <Engine/Frame.h>
#include <Maho.h>

#include <cstdint>

namespace Maho
{

/**
 * Sandbox layer B -- the second independent root, identical in shape to A.
 *
 * Beyond printing, it carries the probe that produces the cross-refactor signal:
 * consecutive frames of the SAME stage must never overlap. That holds today via
 * FGroupGate (per layer) and must still hold after the refactor via the implicit
 * cross-frame self-edge (per node). A violation means two instances of one node ran at
 * once -- which is exactly the failure the gate/self-edge exists to prevent.
 *
 * It also burns a tunable amount of time per tick, so the overlap window is wide enough
 * for serial vs parallel to be visible in the log.
 */
class FTestLayerB : public FFrameExtension, public IPipeline<IInit, ITick, IShutdown>
{
MAHO_DECLARE_FRAME(FTestLayerB);

public:
	void Initialize(FEngineBase&, FEngineContext&) override;
	void Tick(FEngineBase&, FEngineContext&) override;
	void Shutdown(FEngineBase&, FEngineContext&) override;

private:
	std::uint64_t TickCount = 0;
};

} // namespace Maho
