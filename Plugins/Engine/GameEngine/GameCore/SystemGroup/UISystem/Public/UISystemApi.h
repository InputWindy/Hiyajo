#pragma once

#include <Core/Export.h>

#ifdef MAHO_UISYSTEM_MODULE_EXPORTS
#	define MAHO_UISYSTEM_API MAHO_EXPORT
#else
#	define MAHO_UISYSTEM_API MAHO_IMPORT
#endif
