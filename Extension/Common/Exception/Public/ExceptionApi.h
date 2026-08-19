#pragma once

#include <Core/Export.h>

#ifdef MAHO_EXCEPTION_MODULE_EXPORTS
#	define MAHO_EXCEPTION_API MAHO_EXPORT
#else
#	define MAHO_EXCEPTION_API MAHO_IMPORT
#endif
