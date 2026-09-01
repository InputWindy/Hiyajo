#pragma once

#include <Core/Export.h>

#ifdef MAHO_IMGUIRENDER_MODULE_EXPORTS
#	define MAHO_IMGUIRENDER_API MAHO_EXPORT
#else
#	define MAHO_IMGUIRENDER_API MAHO_IMPORT
#endif
