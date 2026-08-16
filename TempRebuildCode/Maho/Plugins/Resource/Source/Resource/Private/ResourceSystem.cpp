#include <ResourceSystem.h>

namespace Maho::Resource
{

bool FResourceSystem::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = Initialize() (start async load thread); Shutdown = Shutdown().
	(void)Stage;
	return true;
}

const char* FResourceSystem::GetThreadName() const
{
	return "Resource";
}

} // namespace Maho::Resource
