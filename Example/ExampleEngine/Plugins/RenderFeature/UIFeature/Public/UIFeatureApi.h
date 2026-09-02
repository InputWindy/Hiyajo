#pragma once

#include <Core/Export.h>

#ifdef MAHO_UIFEATURE_MODULE_EXPORTS
#	define MAHO_UIFEATURE_API MAHO_EXPORT
#else
#	define MAHO_UIFEATURE_API MAHO_IMPORT
#endif
