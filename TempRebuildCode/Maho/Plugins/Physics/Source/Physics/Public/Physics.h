#pragma once

#include "PhysicsApi.h"
#include <Engine.h>

namespace Maho
{

namespace Physics
{

/** Physics simulation library extension (rigid body solver). Pre-app singleton (driven by ESingletonStage). */
class MAHO_PHYSICS_API FPhysics final : public TExtension<ESingletonStage, FPhysics>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FPhysics>;
	FPhysics() = default;
};

} // namespace Physics

} // namespace Maho
