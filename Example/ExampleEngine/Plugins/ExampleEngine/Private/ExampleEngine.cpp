#include "ExampleEngine.h"

#include <Config.h>
#include <Log.h>
#include <Name.h>
#include <Paths.h>
#include <Platform.h>
#include <Render.h>
#include <Resource.h>
#include <Script.h>
#include <Core/FrameGraph.h>   // SetFramesInFlight: the pipeline depth is injected before any graph
#include <GameWorld.h>

#include <Engine/PluginManager.h>

#if defined(_DEBUG) && defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace Maho
{
void FExampleEngine::PreMain()
{
	// -- the pipeline depth (K) -----------------------------------------------------------------
	// Injected ONCE, and it MUST happen here: the flush at the end of this method builds the first
	// frame graph, and that graph's node / event tables are shaped by K for the whole process
	// (`SetFramesInFlight` seals it once a graph exists). The value lives in
	// Config/DefaultEngine.ini and is read STRAIGHT FROM THE FILE rather than through the CVar
	// registry: the layer that pushes [ConsoleVariables] into that registry (`FConfig::IInit`) is a
	// node OF this very batch, so it is too late to serve a value the batch itself needs.
	//
	// CHANGING IT REQUIRES A RESTART. Every consumer of K arrives at its size from this one value --
	// the graph's ring, the RHI's per-slot fences/semaphores/frame command buffers, the staging and
	// render-list buckets, the camera-constant ring, the platform input ring (see
	// FRender::Initialize, which checks all four agree) -- and every one of them sizes itself at
	// Initialize. There is no re-size path, on purpose: K is a hardware-shaped fact of the live node
	// tables and of every ring built on them, so a mid-run change is a bug, not a setting. A runtime
	// re-configuration is a SEPARATE, later change (a safe-point reconfiguration protocol: drain,
	// re-size every ring, re-seal), not something any cvar box may do today.
	{
		const std::int64_t FramesInFlight = Config::ReadIniInt(
			"Config/DefaultEngine.ini", "ConsoleVariables", "r.FramesInFlight", MAHO_FRAMES_IN_FLIGHT);

		// Only the lower bound is refused here; the depth is otherwise whatever the file says. A host
		// that wants to know whether the layers agree asks them (each one reports the depth it sized
		// itself by), so there is no reason left to second-guess the value at the injection point --
		// `SetFramesInFlight` still asserts it is positive.
		SetFramesInFlight(static_cast<std::int32_t>(FramesInFlight > 0 ? FramesInFlight : 1));
	}

#if defined(_DEBUG) && defined(_MSC_VER)
	// Debug CRT 的断言默认弹**模态**对话框：没人点它，整个进程就停在那儿 —— 从外面看就是
	// "编辑器窗口卡死了"，而且断言原文（文件:行 + 表达式）一个字都留不下来。
	// 改成写 stderr：启动器已经把 stderr 重定向到文件（%TEMP%\run_err.txt），于是原文可查、
	// 进程也不再等人。Release/Shipping 里没有断言（NDEBUG），这几行不参与编译。
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
	// Engine layers install from the catalog's install TREE: the project is the root
	// node and the host installs its direct children -- exactly the call a collector
	// layer makes for its own children (InstallChildrenOf(GetName())), one argument
	// apart. Each installed layer then installs ITS children into its own collector,
	// so nothing is installed twice and nobody needs a special "top level" list.
	FPluginManager::Get().Load();
	InstallChildrenOf(GetName());
	FlushPendingUpdates<
		TTypeList<IPreInit, IInit, IPostInit>,
		TTypeList<IPreShutdown, IShutdown, IPostShutdown>
	>();
}

void FExampleEngine::PostMain()
{
	/*
		TODO something
	*/
	FEngineBase::PostMain();
}

std::uint64_t FExampleEngine::NextFrameNumber()
{
	// ONE read per frame of the canonical index -- the same value `Main` then hands to every
	// sequence's Execute for this frame. Read HERE and nowhere else in the frame: two reads could
	// straddle the increment that `FPlatform::IBeginFrame` performs (it runs inside the frame it
	// opens, on a pool thread) and split one frame across two rounds of the ring.
	//
	// The platform is the single writer, so this is a plain atomic load; Execute's monotonic floor
	// covers the window in which the increment for the round just submitted has not landed yet
	// (see FFrameBuilder::Execute -- the reason a stale read must never re-enter a round).
	return Platform::GetFrameIndex();
}

} // namespace Maho

// The C export the host (EntryPoint) looks up BY SYMBOL NAME.
extern "C" MAHO_EXAMPLEENGINE_API Maho::FEngineBase* CreateEngine()
{
	return Maho::FExampleEngine::CreateEngine();
}
