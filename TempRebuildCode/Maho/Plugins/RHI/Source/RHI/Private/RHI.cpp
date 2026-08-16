#include <RHI.h>

namespace Maho::RHI
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

} // namespace Maho::RHI
