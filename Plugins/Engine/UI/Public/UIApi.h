#pragma once

#include <Core/Export.h>

#ifdef MAHO_UI_MODULE_EXPORTS
#	define MAHO_UI_API MAHO_EXPORT
#else
#	define MAHO_UI_API MAHO_IMPORT
#endif
