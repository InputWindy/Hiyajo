#pragma once

#include <Core/Export.h>

#ifdef MAHO_MYPLUGIN_MODULE_EXPORTS
#	define MAHO_MYPLUGIN_API MAHO_EXPORT
#else
#	define MAHO_MYPLUGIN_API MAHO_IMPORT
#endif
