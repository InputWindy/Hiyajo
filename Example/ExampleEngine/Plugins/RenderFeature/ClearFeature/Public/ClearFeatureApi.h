#pragma once

#include <Core/Export.h>

#ifdef MAHO_CLEARFEATURE_MODULE_EXPORTS
#	define MAHO_CLEARFEATURE_API MAHO_EXPORT
#else
#	define MAHO_CLEARFEATURE_API MAHO_IMPORT
#endif
