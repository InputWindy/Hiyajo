#pragma once

#include <Core/Export.h>

#ifdef MAHO_GAMEWORLD_MODULE_EXPORTS
#	define MAHO_GAMEWORLD_API MAHO_EXPORT
#else
#	define MAHO_GAMEWORLD_API MAHO_IMPORT
#endif
