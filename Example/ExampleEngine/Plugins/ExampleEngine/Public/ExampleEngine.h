#pragma once

#include "ExampleEngineApi.h"
#include <Maho.h>
#include <Log.h>

namespace Maho
{

// ExampleEngine — the application root (an FEngineBase). Installs only the input
// driver layer; DynLog/DynWorld/DynRender are dynamically installed/uninstalled
// by GameInput's Tick through the engine's Install/TryUninstall surface.
class FExampleEngine : public FEngineBase
{
MAHO_DECLARE_ENGINE(FExampleEngine, "ExampleEngine.dll");

public:
	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;
};

} // namespace Maho
