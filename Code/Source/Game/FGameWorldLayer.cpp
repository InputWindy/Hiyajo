#include "Game/FGameWorldLayer.h"

#include <ECS/EntityHandle.h>

#include "Game/Components/CameraComponent.h"
#include "Game/Components/MainCameraTag.h"
#include "Game/Components/TransformComponent.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/CameraSystem.h"
#include "Game/Systems/ScriptDispatchSystem.h"

#include <glm/glm.hpp>

FGameWorldLayer::FGameWorldLayer() = default;

void FGameWorldLayer::RegisterSystems(Maho::FSystemGroup& SimGroup)
{
	SimGroup.AddSystem<FMovementSystem>();
	SimGroup.AddSystem<FCameraSystem>();
	SimGroup.AddSystem<Maho::FScriptDispatchSystem>();
}

void FGameWorldLayer::SpawnInitialEntities(Maho::FWorld& World)
{
	// Unit cube at world origin (ForwardRenderer draws a cube per instance).
	{
		Maho::FEntityHandle Handle = World.CreateEntity();
		Maho::FTransformComponent Transform;
		Transform.SetIdentity();
		World.SetComponent<Maho::FTransformComponent>(Handle, Transform);
	}

	// Main camera: above + in front of the cube, tilted 45° down looking at origin.
	{
		Maho::FEntityHandle CamHandle = World.CreateEntity();
		World.AddTag<Maho::FMainCameraTag>(CamHandle);

		const glm::vec3 CamPos(0.0f, 5.0f, 5.0f);
		const glm::vec3 Target(0.0f, 0.0f, 0.0f);

		Maho::FTransformComponent CamTransform;
		CamTransform.SetIdentity();
		CamTransform.Position = CamPos;
		CamTransform.Rotation = glm::quatLookAt(
			glm::normalize(Target - CamPos),
			glm::vec3(0.0f, 1.0f, 0.0f));
		World.SetComponent<Maho::FTransformComponent>(CamHandle, CamTransform);

		Maho::FCameraComponent CamComp;
		CamComp.bMainCamera = true;
		CamComp.FOV = 60.0f;
		CamComp.NearPlane = 0.1f;
		CamComp.FarPlane = 1000.0f;
		CamComp.AspectRatio = 16.0f / 9.0f;
		World.SetComponent<Maho::FCameraComponent>(CamHandle, CamComp);
	}
}
