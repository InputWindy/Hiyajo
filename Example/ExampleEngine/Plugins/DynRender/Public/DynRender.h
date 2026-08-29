#pragma once

#include "DynRenderApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

namespace Maho
{

// DynRender - an engine feature (one node per stage in the engine
// pipeline: BeginFrame -> Tick -> EndFrame).
class FDynRender : public FEngineLayer
{
MAHO_DECLARE_ENGINE_LAYER(FDynRender, "DynRender.dll");

public:
	FDynRender()
	{
		// Cross-DLL dependency: my BeginFrame depends on DynWorld's EndFrame (string addressing).
		AddDependency(std::type_index(typeid(IBeginFrame)), "FDynWorld", std::type_index(typeid(IEndFrame)));
	}

private:
	// -- engine pipeline stages (scheduler-only) --
	void BeginFrame(FEngineBase& Engine) override;
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase& Engine) override;
};

} // namespace Maho
