#include "ExampleEngine.h"

#include <Config.h>
#include <Log.h>
#include <Name.h>
#include <Paths.h>
#include <Platform.h>
#include <Render.h>
#include <Resource.h>
#include <Script.h>
#include <GameWorld.h>

#include <Core/Assembly.h>

#if defined(_DEBUG) && defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace Maho
{
void FExampleEngine::PreMain()
{
	// -- the pipeline depth (K) -----------------------------------------------------------------
	// NOTHING IS INJECTED HERE ANY MORE. K used to be read out of Config/DefaultEngine.ini in this very
	// method and pushed into Core, because the layer that owns the variable (`r.FramesInFlight`) is a
	// node of the batch this method drives -- so the batch itself could not have served a value the
	// batch needed. The authority moved into that layer: `FPlatform`'s static-init installs a reader
	// with Core (`SetFramesInFlightSource`), and Core resolves the value ONCE, on the first frame
	// graph's first use. There is no injection point left, and no second copy that could drift from
	// what the graph's ring is sized by.
	//
	// CHANGING IT STILL REQUIRES A RESTART. Every consumer of K sizes itself from that one value --
	// the graph's ring, the RHI's per-slot fences/semaphores/frame command buffers, the staging and
	// render-list buckets, the camera-constant ring, the platform input ring (see FRender::Initialize,
	// which checks all four agree) -- and every one of them sizes itself at Initialize. There is no
	// re-size path, on purpose: the CVar is flagged ReadOnly, so a mid-run write is refused rather
	// than half-applied. A runtime re-configuration is a SEPARATE, later change (a safe-point
	// reconfiguration protocol: drain, re-size every ring, re-seal), not something any cvar box may
	// do today.
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
	FAssembly::LoadManifest();
	InstallChildrenOf(GetName());
	// The graph the install batch is submitted to belongs to the HOST (this engine object -- see
	// FEngineBase::Graph): created here, on first use, and handed to the driving call (the batch
	// builder stores no graph). It is the first graph of the process, which is why Core resolves K at
	// this moment.
	FFrameGraph& HostGraph = EnsureGraph();
	FlushPendingUpdates<
		TTypeList<IPreInit, IInit, IPostInit>,
		TTypeList<IPreShutdown, IShutdown, IPostShutdown>
	>(HostGraph);
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
