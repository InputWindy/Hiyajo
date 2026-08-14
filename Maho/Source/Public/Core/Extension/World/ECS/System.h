#pragma once

#include <Core/Misc/Export.h>
#include <Core/Engine/EngineStage.h>
#include <Core/Extension/World/ECS/ComponentType.h>

namespace Maho
{

class FWorld;

/**
 * ECS System base.
 *
 * A system reacts to the game world's fine-grained sub-stages through
 * OnCreate / OnDestroy / OnBeginFrame / OnProcessInput / OnFixedUpdate /
 * OnUpdate / OnLateUpdate / OnEndFrame hooks. The owning FSystemGroup passes
 * the FWorld reference (and DeltaTime where relevant) as parameters — systems
 * hold no world pointer and perform no global lookups.
 */
class MAHO_API ISystem
{
public:
	virtual ~ISystem() = default;

	[[nodiscard]] virtual const char* GetName() const = 0;

	virtual void OnCreate(FWorld& World) { (void)World; }
	virtual void OnDestroy(FWorld& World) { (void)World; }
	virtual void OnBeginFrame(FWorld& World) { (void)World; }
	virtual void OnProcessInput(FWorld& World) { (void)World; }
	virtual void OnFixedUpdate(float DeltaTime, FWorld& World) { (void)DeltaTime; (void)World; }
	virtual void OnUpdate(float DeltaTime, FWorld& World) { (void)DeltaTime; (void)World; }
	virtual void OnLateUpdate(float DeltaTime, FWorld& World) { (void)DeltaTime; (void)World; }
	virtual void OnEndFrame(FWorld& World) { (void)World; }
};

/**
 * Declarative helpers for system reads/writes.
 * Used by SystemGroup to derive component masks for automatic ordering.
 */
template <typename... Ts>
struct TReadsComponent
{
	static auto GetMask() { return MakeComponentMask<Ts...>(); }
};

template <typename... Ts>
struct TWritesComponent
{
	static auto GetMask() { return MakeComponentMask<Ts...>(); }
};

} // namespace Maho
