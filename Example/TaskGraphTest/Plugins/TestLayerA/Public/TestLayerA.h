#pragma once

#include "TestLayerAApi.h"

#include <Config.h>
#include <Engine/Engine.h>
#include <Engine/Frame.h>
#include <Exception.h>
#include <Log.h>
#include <Maho.h>
#include <Name.h>
#include <Paths.h>
#include <Text.h>
#include <Timer.h>

#include <cstdint>

namespace Maho
{

/**
 * Sandbox layer A -- one of two independent roots, and the integration probe for the
 * engine's simple service layers.
 *
 * Two jobs:
 *  1. Declare REAL dependency edges onto the Common service layers (so the graph carries
 *     actual cross-layer declarations, not just two empty shells).
 *  2. Assert at Initialize that those services are actually UP. The install tree only
 *     proves a layer was INSTALLED; it does not prove its stages ran. A null accessor is
 *     the observable form of "this layer was silently not driven" -- the failure mode
 *     where a layer lands in a collector whose stage set it does not mount.
 *
 * It also carries the cross-frame exclusivity probe (see the .cpp): consecutive frames of
 * the same stage must never overlap -- true today via FGroupGate, and required after the
 * refactor via the implicit cross-frame self-edge.
 */
class FTestLayerA : public FFrameExtension, public IPipeline<IInit, ITick, IShutdown>
{
MAHO_DECLARE_FRAME(FTestLayerA);

public:
	/** Declares this layer's dependency edges onto the service layers. */
	FTestLayerA();

	void Initialize(FEngineBase&, FEngineContext&) override;
	void Tick(FEngineBase&, FEngineContext&) override;
	void Shutdown(FEngineBase&, FEngineContext&) override;

private:
	std::uint64_t TickCount = 0;
};

} // namespace Maho
