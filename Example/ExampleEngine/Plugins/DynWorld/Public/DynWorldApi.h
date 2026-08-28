#pragma once

#include <Core/Export.h>

#ifdef MAHO_DYNWORLD_MODULE_EXPORTS
#	define MAHO_DYNWORLD_API MAHO_EXPORT
#else
#	define MAHO_DYNWORLD_API MAHO_IMPORT
#endif
