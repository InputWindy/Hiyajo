#pragma once
#include <Core/Export.h>
#ifdef MAHO_TOOLKIT_MODULE_EXPORTS
#	define MAHO_TOOLKIT_API MAHO_EXPORT
#else
#	define MAHO_TOOLKIT_API MAHO_IMPORT
#endif
