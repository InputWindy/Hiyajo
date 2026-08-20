#pragma once

#include <Core/Export.h>

#ifdef MAHO_ASSET_MODULE_EXPORTS
#	define MAHO_ASSET_API MAHO_EXPORT
#else
#	define MAHO_ASSET_API MAHO_IMPORT
#endif

