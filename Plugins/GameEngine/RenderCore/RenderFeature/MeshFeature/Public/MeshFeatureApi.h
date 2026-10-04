#pragma once

#include <Core/Export.h>

#ifdef MAHO_MESHFEATURE_MODULE_EXPORTS
#	define MAHO_MESHFEATURE_API MAHO_EXPORT
#else
#	define MAHO_MESHFEATURE_API MAHO_IMPORT
#endif
