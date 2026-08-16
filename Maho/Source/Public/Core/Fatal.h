#pragma once

#include <Core/Export.h>

namespace Maho
{

/** Unified fatal path: stderr + Saved/Logs/Fatal.log, then abort. */
[[noreturn]] MAHO_API void ReportFatal(const char* Message);

/** Install std::terminate handler once (call from process entry before anything else). */
MAHO_API void InstallFatalHandlers();

} // namespace Maho
