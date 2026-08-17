#pragma once

#include <Core/Export.h>

#ifdef MAHO_JSON_MODULE_EXPORTS
#	define MAHO_JSON_API MAHO_EXPORT
#else
#	define MAHO_JSON_API MAHO_IMPORT
#endif
