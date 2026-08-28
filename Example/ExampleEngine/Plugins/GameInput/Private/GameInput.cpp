#include "GameInput.h"

#include <Log.h>

namespace Maho
{

void FGameInput::BeginFrame(FEngineBase&) {}

void FGameInput::Tick(FEngineBase& Engine)
{
	++TickCount;

	switch (TickCount)
	{
	case 1:
		Engine.Install("DynWorld.dll");
		break;
	case 2:
		Engine.Install("DynRender.dll");
		break;

	// 装齐后跑 1 帧稳定。
	case 3:
		MAHO_LOG_CORE_INFO("[Frame] tick={} (2 features running)", TickCount);
		break;

	// 场景 A：单独匿名卸载被依赖的 World —— 应放弃（卸载不掉）。
	case 4:
		Engine.TryUninstall("FDynWorld");
		break;

	// 场景 A 观察帧：World 仍在（请求被放弃）。
	case 5:
		MAHO_LOG_CORE_INFO("[Frame] tick={} (World uninstall should be dropped)", TickCount);
		break;

	// 场景 B：同帧匿名卸载 World + Render —— 依赖者先弹，连锁卸载两者。
	case 6:
		Engine.TryUninstall("FDynWorld");
		Engine.TryUninstall("FDynRender");
		break;

	// 场景 B 观察帧：只剩 Log。
	case 7:
		MAHO_LOG_CORE_INFO("[Frame] tick={} (only Log should remain)", TickCount);
		break;

	default:
		break;
	}
}

void FGameInput::EndFrame(FEngineBase&) {}

void FGameInput::RequestExit(FEngineBase&)
{
	// 退出逻辑在 Tick 的 default 分支；RequestExit stage 本身 no-op。
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_GAMEINPUT_API Maho::FEngineLayer* CreateLayer()
{
	return Maho::FGameInput::CreateLayer();
}
