#pragma once

#include "GameInputApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

namespace Maho
{

// GameInput — the input driver layer. Its Tick drives per-frame decisions
// (dynamic install / random uninstall / exit) through the engine (Context),
// simulating user input in the engine loop.
class FGameInput : public FEngineLayer
{
MAHO_DECLARE_ENGINE_LAYER(FGameInput, "GameInput.dll");

public:
	void BeginFrame(FEngineBase& Engine) override;
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase& Engine) override;
	void RequestExit(FEngineBase& Engine) override;

private:
	int TickCount = 0;
};

} // namespace Maho
