#pragma once

#include <Core/Export.h>

// This plugin's DLL boundary tag: toggled by codegen, which defines
// MAHO_PLATFORM_MODULE_EXPORTS for the Platform target only -- so the module that DEFINES these
// lives in exports them, and every consumer imports them.
//
// It exists for `GetFrameIndex()`: the canonical frame number is process-wide state, and the norm
// is a free FUNCTION across the DLL boundary rather than a shared variable (MAHO_EXPORT on a data
// symbol would give every module its own copy of the counter -- the "each DLL counts its own
// frames" bug the frame-number work exists to remove).
#ifdef MAHO_PLATFORM_MODULE_EXPORTS
#	define MAHO_PLATFORM_API MAHO_EXPORT
#else
#	define MAHO_PLATFORM_API MAHO_IMPORT
#endif
