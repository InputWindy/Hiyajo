#include <RenderSystem.h>

namespace Maho::Render
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

} // namespace Maho::Render
