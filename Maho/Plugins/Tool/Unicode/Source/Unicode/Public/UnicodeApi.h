#pragma once

#include <Core/Export.h>

#ifdef MAHO_UNICODE_MODULE_EXPORTS
#	define MAHO_UNICODE_API MAHO_EXPORT
#else
#	define MAHO_UNICODE_API MAHO_IMPORT
#endif
