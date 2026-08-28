#pragma once

#include <Core/Export.h>

#ifdef MAHO_GAMEINPUT_MODULE_EXPORTS
#	define MAHO_GAMEINPUT_API MAHO_EXPORT
#else
#	define MAHO_GAMEINPUT_API MAHO_IMPORT
#endif
