#pragma once

#include <ECS/System.h>
#include "Game/Components/TransformComponent.h"

/**
 * Demo system: rotates every entity that has a TransformComponent.
 */
class FMovementSystem final : public Maho::ISystem
{
public:
	[[nodiscard]] const char* GetName() const override { return "MovementSystem"; }
	static const char* StaticName()
	{
		return "MovementSystem";
	}

	void OnUpdate(float DeltaTime, Maho::FWorld& World) override;
};
