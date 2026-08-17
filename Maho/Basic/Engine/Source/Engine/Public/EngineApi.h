#pragma once
#include <Core/Export.h>
#ifdef MAHO_ENGINE_MODULE_EXPORTS
#	define MAHO_ENGINE_API MAHO_EXPORT
#else
#	define MAHO_ENGINE_API MAHO_IMPORT
#endif
