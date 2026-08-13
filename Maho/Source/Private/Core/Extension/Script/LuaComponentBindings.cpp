#include "Core/Extension/Script/LuaComponentBindings.h"

#include <Core/Extension/World/Components/TransformComponent.h>

#include <glm/glm.hpp>

namespace Maho
{

void RegisterLuaComponentBindings(sol::state& Lua)
{
	// ── glm::vec3 ──
	Lua.new_usertype<glm::vec3>("Vec3",
		sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
		"x", &glm::vec3::x,
		"y", &glm::vec3::y,
		"z", &glm::vec3::z);

	// ── FTransformComponent (method-based access; pointer-backed via usertype) ──
	Lua.new_usertype<FTransformComponent>("TransformComponent",
		"set_position", &FTransformComponent::SetPosition,
		"get_position", [](FTransformComponent& T) { return T.Position; },
		"set_scale", &FTransformComponent::SetScale,
		"translate", &FTransformComponent::Translate,
		"rotate_y", &FTransformComponent::RotateY,
		"set_identity", &FTransformComponent::SetIdentity);
}

} // namespace Maho
