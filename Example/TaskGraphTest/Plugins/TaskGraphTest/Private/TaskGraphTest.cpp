#include "TaskGraphTest.h"

#include <Engine/PluginManager.h>

#include <cstdlib>

namespace Maho
{

/** Semantics test for FFrameGraph; defined in FrameGraphSemantics.cpp. */
int RunFrameGraphSemanticsTest();

namespace
{
	/** Set MAHO_TEST_SCHED to run the new scheduler's semantics test instead of the
	 *  normal engine loop. Unset keeps this project as the old-scheduler baseline. */
	bool SchedulerTestMode()
	{
		return std::getenv("MAHO_TEST_SCHED") != nullptr;
	}
}

void FTaskGraphTest::PreMain()
{
	if (SchedulerTestMode())
	{
		// No layers to install: Main() drives FFrameGraph directly. Installing them would
		// only reach the OLD scheduler's collector, which is not what we are testing.
		return;
	}

	// Baseline mode: same shape as the reference project (ExampleEngine).
	FPluginManager::Get().Load();
	InstallChildrenOf(GetName());
	FlushPendingUpdates<
		TTypeList<IPreInit, IInit, IPostInit>,
		TTypeList<IPreShutdown, IShutdown, IPostShutdown>>();
}

int FTaskGraphTest::Main()
{
	if (SchedulerTestMode())
	{
		return RunFrameGraphSemanticsTest();
	}
	return FEngineBase::Main();
}

void FTaskGraphTest::PostMain()
{
	FEngineBase::PostMain();
}

} // namespace Maho

// The C export the host (EntryPoint) looks up BY SYMBOL NAME.
extern "C" MAHO_TASKGRAPHTEST_API Maho::FEngineBase* CreateEngine()
{
	return Maho::FTaskGraphTest::CreateEngine();
}
