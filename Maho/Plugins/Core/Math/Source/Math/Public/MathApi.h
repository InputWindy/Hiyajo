#pragma once

#include <Core/Export.h>

#ifdef MAHO_MATH_MODULE_EXPORTS
#	define MAHO_MATH_API MAHO_EXPORT
#else
#	define MAHO_MATH_API MAHO_IMPORT
#endif
