#pragma once

/** Private: Lua ↔ ECS component bindings (sol2). Included only from Script TUs. */

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace Maho
{

/** Register glm math types + built-in ECS component usertypes. */
void RegisterLuaComponentBindings(sol::state& Lua);

} // namespace Maho
