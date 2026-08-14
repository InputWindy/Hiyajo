#pragma once

#include <Core/Misc/Export.h>

#ifdef MAHO_RESOURCE_MODULE_EXPORTS
#	define MAHO_RESOURCE_API MAHO_EXPORT
#else
#	define MAHO_RESOURCE_API MAHO_IMPORT
#endif
