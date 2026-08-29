// Assembly is now header-only (FAssembly owns a std::unique_ptr module handle;
// Load/Unload/GetProc are defined inline). No out-of-line implementation needed.
// This file stays empty on purpose -- CMake globs it; a future platform-specific
// implementation can move back in here.
#include <Core/Assembly.h>
