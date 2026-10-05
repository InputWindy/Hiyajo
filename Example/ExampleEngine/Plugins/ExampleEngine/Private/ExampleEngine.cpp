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

#include <Engine/PluginManager.h>

#if defined(_DEBUG) && defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace Maho
{
void FExampleEngine::PreMain()
{
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

} // namespace Maho

// The C export the host (EntryPoint) looks up BY SYMBOL NAME.
extern "C" MAHO_EXAMPLEENGINE_API Maho::FEngineBase* CreateEngine()
{
	return Maho::FExampleEngine::CreateEngine();
}
