#pragma once

#include <Core/Export.h>

#ifdef MAHO_EDITORTHEME_MODULE_EXPORTS
#	define MAHO_EDITORTHEME_API MAHO_EXPORT
#else
#	define MAHO_EDITORTHEME_API MAHO_IMPORT
#endif
