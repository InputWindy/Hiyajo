#pragma once

#include <Core/Export.h>

#ifdef MAHO_AI_MODULE_EXPORTS
#	define MAHO_AI_API MAHO_EXPORT
#else
#	define MAHO_AI_API MAHO_IMPORT
#endif
