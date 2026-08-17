#pragma once

#include <Core/Export.h>

#ifdef MAHO_GAMERESOURCE_MODULE_EXPORTS
#	define MAHO_GAMERESOURCE_API MAHO_EXPORT
#else
#	define MAHO_GAMERESOURCE_API MAHO_IMPORT
#endif
