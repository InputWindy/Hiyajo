#pragma once

#include <Core/Export.h>

#ifdef MAHO_WORLD_MODULE_EXPORTS
#	define MAHO_WORLD_API MAHO_EXPORT
#else
#	define MAHO_WORLD_API MAHO_IMPORT
#endif
