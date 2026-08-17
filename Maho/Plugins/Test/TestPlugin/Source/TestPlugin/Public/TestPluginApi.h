#pragma once

#include <Core/Export.h>

#ifdef MAHO_TESTPLUGIN_MODULE_EXPORTS
#	define MAHO_TESTPLUGIN_API MAHO_EXPORT
#else
#	define MAHO_TESTPLUGIN_API MAHO_IMPORT
#endif
