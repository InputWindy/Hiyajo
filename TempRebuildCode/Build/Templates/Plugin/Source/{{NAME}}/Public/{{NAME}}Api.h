#pragma once

#include <Core/Export.h>

#ifdef MAHO_{{EXPORT_NAME}}_MODULE_EXPORTS
#	define MAHO_{{EXPORT_NAME}}_API MAHO_EXPORT
#else
#	define MAHO_{{EXPORT_NAME}}_API MAHO_IMPORT
#endif
