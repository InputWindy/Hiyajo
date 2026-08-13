#pragma once

#include <Core/Extension/World/ECS/ComponentType.h>

#include <string>

namespace Maho
{

class FWorld;

/**
 * ECS System base with multi-stage lifecycle.
 *
 * Override any combination of the virtual hooks below;
 * GetName() is the only pure virtual.
 *
 * Execution order across the frame:
 *   OnCreate -> OnBeginFrame -> OnFixedUpdate*N -> OnUpdate -> OnLateUpdate
 *   -> OnEndFrame -> OnPreRender -> OnPostRender -> OnDestroy
 */
class ISystem
{
public:
	virtual ~ISystem() = default;

	[[nodiscard]] virtual const char* GetName() const = 0;

	/** Called once after the system is registered (Attach stage). */
	virtual void OnCreate(FWorld& World) {}

	/** Called once before the system is destroyed (Detach / Shutdown). */
	virtual void OnDestroy(FWorld& World) {}

	/** Called at the start of every frame. */
	virtual void OnBeginFrame(FWorld& World) {}

	/** Fixed-timestep update (may be called 0..N times per frame). */
	virtual void OnFixedUpdate(float DeltaTime, FWorld& World) {}

	/** Main per-frame update. */
	virtual void OnUpdate(float DeltaTime, FWorld& World) {}

	/** Called after Update, before rendering. */
	virtual void OnLateUpdate(float DeltaTime, FWorld& World) {}

	/** Called at the end of the frame (after LateUpdate). */
	virtual void OnEndFrame(FWorld& World) {}

	/** Called before render submission. Use for render data gathering. */
	virtual void OnPreRender(FWorld& World) {}

	/** Called after rendering completes. */
	virtual void OnPostRender(FWorld& World) {}
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
