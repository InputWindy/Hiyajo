#pragma once

#include <Core/Export.h>

#ifdef MAHO_TIMER_MODULE_EXPORTS
#	define MAHO_TIMER_API MAHO_EXPORT
#else
#	define MAHO_TIMER_API MAHO_IMPORT
#endif
