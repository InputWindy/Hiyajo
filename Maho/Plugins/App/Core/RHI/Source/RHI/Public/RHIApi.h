#pragma once

#include <Core/Export.h>

#ifdef MAHO_RHI_MODULE_EXPORTS
#	define MAHO_RHI_API MAHO_EXPORT
#else
#	define MAHO_RHI_API MAHO_IMPORT
#endif
