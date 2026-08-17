#pragma once

#include <Core/Export.h>

#ifdef MAHO_PLATFORM_MODULE_EXPORTS
#	define MAHO_PLATFORM_API MAHO_EXPORT
#else
#	define MAHO_PLATFORM_API MAHO_IMPORT
#endif
