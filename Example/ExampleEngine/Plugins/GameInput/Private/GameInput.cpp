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

	// Run one frame to stabilize once everything is installed.
	case 3:
		MAHO_LOG_CORE_INFO("[Frame] tick={} (2 features running)", TickCount);
		break;

	// Scenario A: anonymous uninstall of a depended-on World - should be dropped (cannot uninstall).
	case 4:
		Engine.TryUninstall("FDynWorld");
		break;

	// Scenario A observation frame: World is still there (request was dropped).
	case 5:
		MAHO_LOG_CORE_INFO("[Frame] tick={} (World uninstall should be dropped)", TickCount);
		break;

	// Scenario B: anonymous uninstall of World + Render in the same frame -
	// the dependent pops first, then both are uninstalled in a chain.
	case 6:
		Engine.TryUninstall("FDynWorld");
		Engine.TryUninstall("FDynRender");
		break;

	// Scenario B observation frame: only Log remains.
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
	// Exit logic lives in Tick's default branch; the RequestExit stage itself is a no-op.
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_GAMEINPUT_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FGameInput::CreateLayer();
}
