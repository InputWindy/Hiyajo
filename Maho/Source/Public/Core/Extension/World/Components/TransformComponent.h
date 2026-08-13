#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <type_traits>

namespace Maho
{

/**
 * ECS component: position / rotation / scale as source data,
 * LocalToWorld as a derived matrix cache (compute on demand).
 */
struct FTransformComponent
{
	glm::vec3 Position{0.0f, 0.0f, 0.0f};
	glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec3 Scale{1.0f, 1.0f, 1.0f};
	glm::mat4 LocalToWorld{1.0f};

	void SetIdentity()
	{
		Position = glm::vec3(0.0f);
		Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		Scale = glm::vec3(1.0f);
		LocalToWorld = glm::mat4(1.0f);
	}

	void ComputeLocalToWorld()
	{
		const glm::mat4 T = glm::translate(glm::mat4(1.0f), Position);
		const glm::mat4 R = glm::mat4_cast(Rotation);
		const glm::mat4 S = glm::scale(glm::mat4(1.0f), Scale);
		LocalToWorld = T * R * S;
	}

	void SetPosition(float X, float Y, float Z)
	{
		Position = glm::vec3(X, Y, Z);
	}

	void SetScale(float X, float Y, float Z)
	{
		Scale = glm::vec3(X, Y, Z);
	}

	void Translate(float Dx, float Dy, float Dz)
	{
		Position += glm::vec3(Dx, Dy, Dz);
	}

	void RotateY(float Radians)
	{
		Rotation = glm::angleAxis(Radians, glm::vec3(0.0f, 1.0f, 0.0f)) * Rotation;
	}
};

} // namespace Maho

static_assert(std::is_trivially_copyable_v<Maho::FTransformComponent>, "FTransformComponent must be trivially copyable");
