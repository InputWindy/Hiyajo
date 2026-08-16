#pragma once

#include <type_traits>

namespace Maho
{

struct FCameraComponent
{
	float FOV = 60.0f;
	float NearPlane = 0.1f;
	float FarPlane = 1000.0f;
	float AspectRatio = 16.0f / 9.0f;
	bool bMainCamera = false;
	bool bOrthographic = false;
	float OrthoSize = 10.0f;

	void SetPerspective(float InFOV, float InNear, float InFar)
	{
		FOV = InFOV;
		NearPlane = InNear;
		FarPlane = InFar;
		bOrthographic = false;
	}

	void SetOrthographic(float InSize, float InNear, float InFar)
	{
		OrthoSize = InSize;
		NearPlane = InNear;
		FarPlane = InFar;
		bOrthographic = true;
	}
};

} // namespace Maho

static_assert(std::is_trivially_copyable_v<Maho::FCameraComponent>, "FCameraComponent must be trivially copyable");
