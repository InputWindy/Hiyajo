// Log is a CRTP SINGLETON (TSingleton<FLog>) — no per-instance factory needed.
// It lives as a DLL so depending plugins can link spdlog through it; the
// singleton is reached via FLog::Get()/Instance(), never CreateExtension.
#include "Log.h"

namespace Maho
{
}
