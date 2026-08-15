#include <RHI.h>

namespace Maho
{

bool FRHI::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = Initialize() (start RHI thread); Shutdown = Shutdown().
	(void)Stage;
	return true;
}

const char* FRHI::GetThreadName() const
{
	return "RHI";
}

} // namespace Maho
