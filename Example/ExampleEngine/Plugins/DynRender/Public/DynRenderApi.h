#pragma once

#include <Core/Export.h>

#ifdef MAHO_DYNRENDER_MODULE_EXPORTS
#	define MAHO_DYNRENDER_API MAHO_EXPORT
#else
#	define MAHO_DYNRENDER_API MAHO_IMPORT
#endif
