#pragma once

#include "TaskGraphTestApi.h"
#include <Maho.h>

namespace Maho
{

// TaskGraphTest - the application root (an FEngineBase).
// Implement PreMain/PostMain to install layers; the engine drives the loop.
class FTaskGraphTest : public FEngineBase
{
MAHO_DECLARE_ENGINE(FTaskGraphTest);

public:
	void PreMain() override;
	int Main() override;
	void PostMain() override;
};

} // namespace Maho
