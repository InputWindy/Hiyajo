#pragma once

#include <Core/Export.h>

#ifdef MAHO_TESTLAYERA_MODULE_EXPORTS
#	define MAHO_TESTLAYERA_API MAHO_EXPORT
#else
#	define MAHO_TESTLAYERA_API MAHO_IMPORT
#endif
