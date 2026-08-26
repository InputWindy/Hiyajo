// Reflect is a header-only library (refl-cpp) — no .cpp implementation.
// This TU exists so codegen produces a target that carries the include dirs
// (and the transitive reflcpp::reflcpp link) to dependents.
#include "Reflect.h"

// NOTE: MAHO_REFLECT/REFL_AUTO must be expanded in the GLOBAL namespace
// (refl-cpp injects `namespace refl_impl::metadata` at global scope).

// compile-time smoke: the syntax-sugar macros expand against real refl-cpp API
struct FReflectSmoke
{
	int X = 0;
	int Y = 0;
};
MAHO_REFLECT(FReflectSmoke, field(X), field(Y));
static_assert(MAHO_MEMBER_COUNT(FReflectSmoke) == 2, "MAHO_REFLECT/MEMBER_COUNT");

namespace Maho
{
namespace Reflect
{
	// pure library — nothing else to define
}
}
