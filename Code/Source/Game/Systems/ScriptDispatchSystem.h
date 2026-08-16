#pragma once

#include <ECS/System.h>

#include <memory>

namespace Maho
{

/**
 * Game system that drives per-entity Lua scripts (same level as FMovementSystem).
 * Maps ISystem sub-stage hooks to per-entity script hooks. Owns the script
 * prototype cache + per-entity instance tables.
 */
class FScriptDispatchSystem : public ISystem
{
public:
	FScriptDispatchSystem();
	~FScriptDispatchSystem() override;

	[[nodiscard]] const char* GetName() const override
	{
		return "ScriptDispatchSystem";
	}

	void OnBeginFrame(FWorld& World) override;
	void OnProcessInput(FWorld& World) override;
	void OnFixedUpdate(float DeltaTime, FWorld& World) override;
	void OnUpdate(float DeltaTime, FWorld& World) override;
	void OnLateUpdate(float DeltaTime, FWorld& World) override;
	void OnEndFrame(FWorld& World) override;

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;

	void DispatchStage(FWorld& InWorld, float DeltaTime, const char* HookName);
};

} // namespace Maho
