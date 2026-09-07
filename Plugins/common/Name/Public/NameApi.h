#pragma once

#include <Core/Export.h>

#ifdef MAHO_NAME_MODULE_EXPORTS
#	define MAHO_NAME_API MAHO_EXPORT
#else
#	define MAHO_NAME_API MAHO_IMPORT
#endif
