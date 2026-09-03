#pragma once

#include <Core/Export.h>

#ifdef MAHO_FRAME_MODULE_EXPORTS
#	define MAHO_FRAME_API MAHO_EXPORT
#else
#	define MAHO_FRAME_API MAHO_IMPORT
#endif
