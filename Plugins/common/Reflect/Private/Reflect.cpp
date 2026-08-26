// Reflect is a header-only library (refl-cpp) — no .cpp implementation.
// This TU exists so codegen produces a target that carries the include dirs
// (and the transitive reflcpp::reflcpp link) to dependents.
#include "Reflect.h"

namespace Maho
{
namespace Reflect
{
	// pure library — nothing to define
}
}
