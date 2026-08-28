#include "ExampleEngine.h"

namespace Maho
{

void FExampleEngine::Initialize(int Argc, char** Argv)
{
	FLog::Get().Initialize(Argc, Argv);
	MAHO_LOG_CORE_INFO("FExampleEngine::Initialize — input-driven install/uninstall test");

	// 只装输入驱动层；DynLog/DynWorld/DynRender 由 GameInput 的 Tick 逐帧动态安装。
	Install("GameInput.dll");
}

void FExampleEngine::Shutdown()
{
	// 引擎 base 已 delete 全部 feature + FreeLibrary（FEngineBase::Shutdown）。
	FLog::Get().Shutdown();
}

} // namespace Maho

// The C export the host (EntryPoint) looks up BY SYMBOL NAME.
extern "C" MAHO_EXAMPLEENGINE_API Maho::FEngineBase* CreateEngine()
{
	return Maho::FExampleEngine::CreateEngine();
}
