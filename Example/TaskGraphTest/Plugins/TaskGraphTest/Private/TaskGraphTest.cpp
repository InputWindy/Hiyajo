#include "TaskGraphTest.h"

#include <Core/Assembly.h>

#include <cstdlib>

namespace Maho
{

/** Semantics test for FFrameGraph; defined in FrameGraphSemantics.cpp. */
int RunFrameGraphSemanticsTest();

/** Test for the execution backend (Core/Task/TaskGraph.h); defined in TaskGraphBackend.cpp. */
int RunTaskGraphBackendTest();

/** TEMPORARY verification of the ConsoleVariable lazy-config / ReadOnly changes;
 *  defined in CVarConfigVerify.cpp. Driven by MAHO_TEST_CVAR. */
int RunConsoleVariableConfigTest();

namespace
{
	/** Set MAHO_TEST_SCHED to run the new scheduler's semantics test instead of the
	 *  normal engine loop. Unset keeps this project as the old-scheduler baseline. */
	bool SchedulerTestMode()
	{
		return std::getenv("MAHO_TEST_SCHED") != nullptr;
	}

	/** TEMPORARY: set MAHO_TEST_CVAR to run the ConsoleVariable verification. The plugin DLLs are
	 *  still loaded (their static CVars register), but NO layer is installed -- in particular not
	 *  FConfig, so the value a read returns can only have come from ConsoleVariable's own lazy
	 *  ini application, never from FConfig::Initialize pushing it through Set. */
	bool CVarTestMode()
	{
		return std::getenv("MAHO_TEST_CVAR") != nullptr;
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

	FAssembly::LoadManifest();
	if (CVarTestMode())
	{
		// DLLs only: InstallChildrenOf maps every child module (so the CVars its statics declare --
		// e.g. FLog.dll's `r.Trace` -- are registered), but NOTHING is flushed, so no stage ever
		// runs: FConfig::Initialize in particular, i.e. no layer pushes [ConsoleVariables] through
		// Set. Whatever a read returns therefore came from ConsoleVariable's own lazy ini apply.
		InstallChildrenOf(GetName());
		return;
	}

	// Baseline mode: same shape as the reference project (ExampleEngine).
	InstallChildrenOf(GetName());
	// The graph belongs to the HOST (FEngineBase::Graph) and is created on first use -- here, before
	// the install batch that needs it, which is handed the reference (the builder stores no graph).
	// SchedulerTestMode/CVarTestMode above drive no batch at all.
	FFrameGraph& HostGraph = EnsureGraph();
	FlushPendingUpdates<
		TTypeList<IPreInit, IInit, IPostInit>,
		TTypeList<IPreShutdown, IShutdown, IPostShutdown>>(HostGraph);
}

int FTaskGraphTest::Main()
{
	if (SchedulerTestMode())
	{
		// Both suites: the frame system's semantics (T*) and the execution backend under it (B*).
		const int Semantics = RunFrameGraphSemanticsTest();
		const int Backend = RunTaskGraphBackendTest();
		return (Semantics == 0 && Backend == 0) ? 0 : 1;
	}
	if (CVarTestMode())
	{
		return RunConsoleVariableConfigTest();
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
