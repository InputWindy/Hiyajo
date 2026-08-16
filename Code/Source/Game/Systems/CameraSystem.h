#pragma once

#include <ECS/System.h>

class FCameraSystem : public Maho::ISystem
{
public:
	FCameraSystem() = default;
	void OnUpdate(float DeltaTime, Maho::FWorld& World) override;
	[[nodiscard]] const char* GetName() const override
	{
		return "CameraSystem";
	}
	static const char* StaticName()
	{
		return "CameraSystem";
	}
};
