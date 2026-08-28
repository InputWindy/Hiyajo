#pragma once

#include <Core/Export.h>

#ifdef MAHO_DYNLOG_MODULE_EXPORTS
#	define MAHO_DYNLOG_API MAHO_EXPORT
#else
#	define MAHO_DYNLOG_API MAHO_IMPORT
#endif
