#pragma once

#include <Core/Export.h>

#ifdef MAHO_PATHS_MODULE_EXPORTS
#	define MAHO_PATHS_API MAHO_EXPORT
#else
#	define MAHO_PATHS_API MAHO_IMPORT
#endif
