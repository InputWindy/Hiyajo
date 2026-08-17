#pragma once

#include <Core/Export.h>

#ifdef MAHO_EDITOR_MODULE_EXPORTS
#	define MAHO_EDITOR_API MAHO_EXPORT
#else
#	define MAHO_EDITOR_API MAHO_IMPORT
#endif
