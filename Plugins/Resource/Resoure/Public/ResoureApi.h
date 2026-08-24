#pragma once

#include <Core/Export.h>

#ifdef MAHO_RESOURE_MODULE_EXPORTS
#	define MAHO_RESOURE_API MAHO_EXPORT
#else
#	define MAHO_RESOURE_API MAHO_IMPORT
#endif
