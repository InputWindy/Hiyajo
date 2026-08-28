#pragma once

#include "DynRenderApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

namespace Maho
{

// DynRender — an engine feature (one node per stage in the engine
// pipeline: BeginFrame → Tick → EndFrame).
class FDynRender : public FEngineLayer
{
MAHO_DECLARE_LAYER(FDynRender);
MAHO_DECLARE_ENGINE_LAYER(FDynRender, "DynRender.dll");

public:
	FDynRender()
	{
		// 跨 DLL 依赖：我的 BeginFrame 依赖 DynWorld 的 EndFrame（字符串寻址）。
		AddDependency(std::type_index(typeid(IBeginFrame)), "FDynWorld", std::type_index(typeid(IEndFrame)));
	}

	void BeginFrame(FEngineBase& Engine) override;
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase& Engine) override;
};

} // namespace Maho
