#pragma once

#include "GameInputApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

namespace Maho
{

// GameInput — the input driver layer. Its Tick drives per-frame decisions
// (dynamic install / random uninstall / exit) through the owner FEngineBase,
// simulating user input in the engine loop.
class FGameInput : public FEngineLayer
{
MAHO_DECLARE_LAYER(FGameInput);
MAHO_DECLARE_FEATURE(FGameInput, "GameInput.dll");

public:
	void BeginFrame() override;
	void Tick() override;
	void EndFrame() override;

private:
	int TickCount = 0;
};

} // namespace Maho
