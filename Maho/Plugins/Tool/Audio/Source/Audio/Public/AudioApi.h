#pragma once

#include <Core/Export.h>

#ifdef MAHO_AUDIO_MODULE_EXPORTS
#	define MAHO_AUDIO_API MAHO_EXPORT
#else
#	define MAHO_AUDIO_API MAHO_IMPORT
#endif
