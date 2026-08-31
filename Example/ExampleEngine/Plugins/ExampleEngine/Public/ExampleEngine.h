#pragma once

#include "ExampleEngineApi.h"
#include <Maho.h>
#include <Log.h>

namespace Maho
{

// ExampleEngine - the application root (an FEngineBase). PreMain installs the
// engine service layers (Log/Config/Platform/Resource/Script/Render); the
// render subsystem (FRender) drives the Scene/DrawTriangleFeature features on
// its own render thread, and FPlatform requests exit when the window closes.
class FExampleEngine : public FEngineBase
{
MAHO_DECLARE_ENGINE(FExampleEngine, "ExampleEngine.dll");

public:
	void PreMain() override;

	void PostMain() override;
};

} // namespace Maho
