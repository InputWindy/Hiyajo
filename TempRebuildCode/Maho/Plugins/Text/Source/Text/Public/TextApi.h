#pragma once

#include <Core/Export.h>

#ifdef MAHO_TEXT_MODULE_EXPORTS
#	define MAHO_TEXT_API MAHO_EXPORT
#else
#	define MAHO_TEXT_API MAHO_IMPORT
#endif
