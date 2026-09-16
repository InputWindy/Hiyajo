#pragma once

#include <Core/Export.h>

#ifdef MAHO_TESTLAYERB_MODULE_EXPORTS
#	define MAHO_TESTLAYERB_API MAHO_EXPORT
#else
#	define MAHO_TESTLAYERB_API MAHO_IMPORT
#endif
