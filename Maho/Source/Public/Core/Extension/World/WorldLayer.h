#pragma once

#include <Core/ECS/World.h>
#include <Core/ECS/SystemGroup.h>
#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/Sequencer/EngineStage.h>

#include <string>

namespace Maho
{
class FScriptSystem;
class FTransformComponent;
}

/**
 * Owns the ECS world (pure data) + the root system group (driver skeleton).
 * Maps engine stages to SystemGroup lifecycle hooks, and dispatches the
 * matching script stage hook to entities carrying FScriptComponent.
 *
 * Game projects subclass this and override RegisterSystems / SpawnInitialEntities
 * to inject their own systems and initial entities.
 */
class MAHO_API FWorldLayer : public Maho::FLayer
{
public:
	explicit FWorldLayer(std::string WorldName = "MainWorld");
	~FWorldLayer() override = default;

	bool ExecuteStage(Maho::EEngineStage Stage) override;

	[[nodiscard]] Maho::FWorld& GetWorld() { return World; }
	[[nodiscard]] const Maho::FWorld& GetWorld() const { return World; }

protected:
	/** Register game systems into the simulation group during Attach. */
	virtual void RegisterSystems(Maho::FSystemGroup& SimGroup) {}

	/** Spawn initial entities (camera, demo actors, etc.) during Attach. */
	virtual void SpawnInitialEntities(Maho::FWorld& World) {}

	std::string WorldName;
	Maho::FWorld World;
	Maho::FInitializationSystemGroup RootGroup;

private:
	/** Dispatch one EEngineStage to every entity with a script component. */
	void DispatchScriptStage(Maho::EEngineStage Stage, float DeltaTime);

	bool bWorldReady = false;
};
