#pragma once

#include "DynWorldApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

namespace Maho
{

// DynWorld — an engine feature (one node per stage in the engine
// pipeline: BeginFrame → Tick → EndFrame).
class FDynWorld : public FEngineLayer
{
MAHO_DECLARE_LAYER(FDynWorld);
MAHO_DECLARE_ENGINE_LAYER(FDynWorld, "DynWorld.dll");

public:
	FDynWorld()
	{
		// 跨 DLL 依赖：我的 Tick 依赖 DynLog 的 EndFrame（字符串寻址）。
		AddDependency(std::type_index(typeid(ITick)), "FDynLog", std::type_index(typeid(IEndFrame)));
	}

	void BeginFrame(FEngineBase& Engine) override;
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase& Engine) override;
};

} // namespace Maho
