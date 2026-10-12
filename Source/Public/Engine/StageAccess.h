#pragma once

// StageAccess -- the SHORT access vocabulary for a stage's declaration tail (change
// add-resource-metadata-edges, step 2):
//
//   MAHO_IMPLEMENT_STAGE(FGameWorld, IShutdown, Shutdown,
//       Destroy(GameWorld::FComponentPools),
//       Read(FLog), Read(IRHI))
//
// ── WHY THIS IS A SEPARATE HEADER, AND WHY YOU INCLUDE IT LAST ──────────────────────────────────
//
// `Create`, `Read`, `Write` and `Destroy` are the words the declaration reads best with, and they are
// also ordinary METHOD NAMES all over this codebase -- `FEntityRegistry::Create()`,
// `TResourceCreator<T>::Create(Path)`, `FRHIFactory::Create(Backend)`, the input probes' `Read(...)`,
// a plugin's own `void Read(const char*, ...)`. A function-like macro bearing one of those names
// rewrites EVERY use of the identifier followed by `(` -- including a method definition's parameter
// list, where the expansion is a syntax error rather than something the compiler could diagnose
// usefully.
//
// So the short names are OPT-IN, per translation unit, and the include belongs at the POINT OF USE:
// immediately above the `MAHO_IMPLEMENT_STAGE` block, after that file's own code. A translation unit
// that includes this header and then writes `Registry.Create()` WILL fail to compile -- which is the
// honest outcome: the two spellings cannot coexist in one TU, and saying so once, loudly, at the one
// place a declaration site needs them, is better than a rename that makes every declaration site read
// like the internals of a table.
//
// The prefixed forms (`MAHO_ACCESS_CREATE(R)`, ...) are always available and mean exactly the same
// thing -- use them in a HEADER, where a leaked short name would poison every includer.

#include <Engine/StageMetadata.h>

/** `Create(X)` -- the resource X's lifetime starts at this stage. Expands to an `FResourceAccess`. */
#define Create(R)  MAHO_ACCESS_CREATE(R)
/** `Read(X)` -- this stage reads the resource X. Expands to an `FResourceAccess`. */
#define Read(R)    MAHO_ACCESS_READ(R)
/** `Write(X)` -- this stage may modify the resource X. Expands to an `FResourceAccess`. */
#define Write(R)   MAHO_ACCESS_WRITE(R)
/** `Destroy(X)` -- the resource X's lifetime ends at this stage. Expands to an `FResourceAccess`. */
#define Destroy(R) MAHO_ACCESS_DESTROY(R)
