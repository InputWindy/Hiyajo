#pragma once

#include <Core/Export.h>

// DLL export tag for the ConsoleVariable plugin. Building ConsoleVariable.dll
// defines MAHO_CONSOLEVARIABLE_MODULE_EXPORTS (see ConsoleVariable.cmake) => the
// tag expands to MAHO_EXPORT; every consumer (FConfig, FLog, Platform, Render,
// ...) expands it to MAHO_IMPORT, so there is exactly ONE registry instance in
// the process - the one inside ConsoleVariable.dll.
//
// The tag is on the class (not only on `Get()`): a plugin's TAutoConsoleVariable
// statics call Register/Find through that instance, and an untagged class has no
// key function in the consumer's eyes - its vftable + deleting dtor would be
// re-emitted per module. See root AGENTS.md, "Export / Module-Boundary Rules".
#ifdef MAHO_CONSOLEVARIABLE_MODULE_EXPORTS
#	define MAHO_CONSOLEVARIABLE_API MAHO_EXPORT
#else
#	define MAHO_CONSOLEVARIABLE_API MAHO_IMPORT
#endif
