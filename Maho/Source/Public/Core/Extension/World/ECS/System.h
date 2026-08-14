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
 * A system responds to engine stages through a single hook:
 *   virtual bool ExecuteStage(EEngineStage Stage, float DeltaTime, FWorld& World)
 *
 * The owning FSystemGroup passes the FWorld reference and the stage DeltaTime
 * as parameters — systems hold no world pointer and perform no global lookups.
 */
class MAHO_API ISystem
{
public:
	virtual ~ISystem() = default;

	[[nodiscard]] virtual const char* GetName() const = 0;

	/**
	 * Drive one engine stage. DeltaTime is the frame delta for Update/LateUpdate
	 * and the fixed delta for FixedUpdate (0 otherwise). World is the ECS world.
	 */
	virtual bool ExecuteStage(EEngineStage Stage, float DeltaTime, FWorld& World)
	{
		(void)Stage;
		(void)DeltaTime;
		(void)World;
		return true;
	}
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
