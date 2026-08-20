#pragma once

#include <Core/Export.h>

#ifdef MAHO_MATHY_MODULE_EXPORTS
#	define MAHO_MATHY_API MAHO_EXPORT
#else
#	define MAHO_MATHY_API MAHO_IMPORT
#endif
