#pragma once

#include <Core/Export.h>

#ifdef MAHO_LOG_MODULE_EXPORTS
#	define MAHO_LOG_API MAHO_EXPORT
#else
#	define MAHO_LOG_API MAHO_IMPORT
#endif
