#pragma once

#include <Core/Export.h>

#ifdef MAHO_SCENE_MODULE_EXPORTS
#	define MAHO_SCENE_API MAHO_EXPORT
#else
#	define MAHO_SCENE_API MAHO_IMPORT
#endif
