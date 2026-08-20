#pragma once

#include <Core/Export.h>

#ifdef MAHO_ARCHIVE_MODULE_EXPORTS
#	define MAHO_ARCHIVE_API MAHO_EXPORT
#else
#	define MAHO_ARCHIVE_API MAHO_IMPORT
#endif

