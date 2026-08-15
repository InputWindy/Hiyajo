#pragma once

#include <Core/Export.h>

#ifdef MAHO_RENDER_MODULE_EXPORTS
#	define MAHO_RENDER_API MAHO_EXPORT
#else
#	define MAHO_RENDER_API MAHO_IMPORT
#endif
