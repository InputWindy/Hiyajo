#pragma once

#include <Core/Export.h>

#ifdef MAHO_COMPRESS_MODULE_EXPORTS
#	define MAHO_COMPRESS_API MAHO_EXPORT
#else
#	define MAHO_COMPRESS_API MAHO_IMPORT
#endif
