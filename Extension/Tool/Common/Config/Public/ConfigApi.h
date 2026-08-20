#pragma once

#include <Core/Export.h>

#ifdef MAHO_CONFIG_MODULE_EXPORTS
#	define MAHO_CONFIG_API MAHO_EXPORT
#else
#	define MAHO_CONFIG_API MAHO_IMPORT
#endif

