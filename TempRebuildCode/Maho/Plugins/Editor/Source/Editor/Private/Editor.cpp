#include <Editor.h>

namespace Maho::Editor
{

bool FEditorSystem::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = build editor UI; Tick = draw; Shutdown = tear down.
	(void)Stage;
	return true;
}

} // namespace Maho::Editor
