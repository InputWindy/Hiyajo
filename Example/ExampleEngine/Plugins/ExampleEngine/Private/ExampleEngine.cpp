#include "ExampleEngine.h"

#include <Log.h>

namespace Maho
{
void FExampleEngine::PreMain()
{
	// 引擎服务层（Log）与输入驱动层提前安装；DynLog/DynWorld/DynRender
	// 由 GameInput 的 Tick 逐帧动态安装。
	Install("Log.dll");
	Install("GameInput.dll");
	Install("Platform.dll");
}

void FExampleEngine::PostMain()
{
}

} // namespace Maho

// The C export the host (EntryPoint) looks up BY SYMBOL NAME.
extern "C" MAHO_EXAMPLEENGINE_API Maho::FEngineBase* CreateEngine()
{
	return Maho::FExampleEngine::CreateEngine();
}
