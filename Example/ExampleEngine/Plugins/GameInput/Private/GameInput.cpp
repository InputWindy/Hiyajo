#include "GameInput.h"

#include <Log.h>

namespace Maho
{

void FGameInput::BeginFrame() {}

void FGameInput::Tick()
{
	++TickCount;

	switch (TickCount)
	{
	case 1:
		Owner->Install("DynLog.dll");
		break;
	case 2:
		Owner->Install("DynWorld.dll");
		break;
	case 3:
		Owner->Install("DynRender.dll");
		break;

	// 装齐后跑 1 帧稳定。
	case 4:
		MAHO_LOG_CORE_INFO("[Frame] tick={} (3 features running)", TickCount);
		break;

	// 场景 A：单独匿名卸载被依赖的 World —— 应放弃（卸载不掉）。
	case 5:
		Owner->TryUninstall("FDynWorld");
		break;

	// 场景 A 观察帧：World 仍在（请求被放弃）。
	case 6:
		MAHO_LOG_CORE_INFO("[Frame] tick={} (World uninstall should be dropped)", TickCount);
		break;

	// 场景 B：同帧匿名卸载 World + Render —— 依赖者先弹，连锁卸载两者。
	case 7:
		Owner->TryUninstall("FDynWorld");
		Owner->TryUninstall("FDynRender");
		break;

	// 场景 B 观察帧：只剩 Log。
	case 8:
		MAHO_LOG_CORE_INFO("[Frame] tick={} (only Log should remain)", TickCount);
		break;

	default:
		Owner->RequestExit();
		break;
	}
}

void FGameInput::EndFrame() {}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_GAMEINPUT_API Maho::FEngineLayer* CreateLayer()
{
	return Maho::FGameInput::CreateLayer();
}
