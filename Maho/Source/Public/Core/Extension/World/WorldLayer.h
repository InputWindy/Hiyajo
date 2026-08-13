#pragma once

#include <Core/Extension/World/ECS/World.h>
#include <Core/Extension/World/ECS/SystemGroup.h>
#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/Sequencer/EngineStage.h>

#include <string>

namespace Maho
{
class FScriptSystem;
class FTransformComponent;

/**
 * Owns the ECS world (pure data) + the root system group (driver skeleton).
 * Maps engine stages to SystemGroup lifecycle hooks, and dispatches the
 * matching script stage hook to entities carrying FScriptComponent.
 *
 * Game projects subclass this and override RegisterSystems / SpawnInitialEntities
 * to inject their own systems and initial entities.
 */
class MAHO_API FWorldLayer : public FLayer
{
public:
	explicit FWorldLayer(std::string WorldName = "MainWorld");
	~FWorldLayer() override = default;

	bool ExecuteStage(EEngineStage Stage) override;

	[[nodiscard]] FWorld& GetWorld() { return World; }
	[[nodiscard]] const FWorld& GetWorld() const { return World; }

protected:
	/** Register game systems into the simulation group during Attach. */
	virtual void RegisterSystems(FSystemGroup& SimGroup) {}

	/** Spawn initial entities (camera, demo actors, etc.) during Attach. */
	virtual void SpawnInitialEntities(FWorld& World) {}

	std::string WorldName;
	FWorld World;
	FInitializationSystemGroup RootGroup;

private:
	/** Dispatch one EEngineStage to every entity with a script component. */
	void DispatchScriptStage(EEngineStage Stage, float DeltaTime);

	bool bWorldReady = false;
};

} // namespace Maho
