#include <RenderSystem.h>

namespace Maho
{

bool FRenderSystem::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = Initialize() (start render thread); Shutdown = Shutdown().
	(void)Stage;
	return true;
}

const char* FRenderSystem::GetThreadName() const
{
	return "Render";
}

} // namespace Maho
