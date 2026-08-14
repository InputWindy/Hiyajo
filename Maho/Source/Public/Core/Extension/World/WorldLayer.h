#pragma once

#include <Core/Extension/World/ECS/World.h>
#include <Core/Extension/World/ECS/SystemGroup.h>
#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/Sequencer/EngineStage.h>

#include <string>

namespace Maho
{

/**
 * Pure world scaffold. Owns the ECS world (pure data) + the root system group
 * (driver skeleton), and maps engine stages to SystemGroup lifecycle hooks.
 *
 * Concrete world components, script stage dispatch, and any other project
 * gameplay logic belong in the game project: subclass FWorldLayer and override
 * RegisterSystems / SpawnInitialEntities / OnStageDispatched.
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

	/** Project hook: called after the RootGroup tick for a stage. */
	virtual void OnStageDispatched(EEngineStage Stage, float DeltaTime)
	{
		(void)Stage;
		(void)DeltaTime;
	}

	std::string WorldName;
	FWorld World;
	FInitializationSystemGroup RootGroup;

private:
	bool bWorldReady = false;
};

} // namespace Maho
