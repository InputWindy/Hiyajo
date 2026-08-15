#include <Compress.h>

namespace Maho
{

bool FCompress::ExecuteStage(ESingletonStage Stage)
{
	// TODO: Init = init codecs; Shutdown = release.
	(void)Stage;
	return true;
}

} // namespace Maho
