#pragma once

#include <Core/Export.h>

#ifdef MAHO_SCRIPT_MODULE_EXPORTS
#	define MAHO_SCRIPT_API MAHO_EXPORT
#else
#	define MAHO_SCRIPT_API MAHO_IMPORT
#endif
