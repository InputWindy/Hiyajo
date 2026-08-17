#pragma once

#include <Core/Export.h>

#ifdef MAHO_NETWORK_MODULE_EXPORTS
#	define MAHO_NETWORK_API MAHO_EXPORT
#else
#	define MAHO_NETWORK_API MAHO_IMPORT
#endif
