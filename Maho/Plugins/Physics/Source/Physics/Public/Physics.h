#pragma once

#include "PhysicsApi.h"
#include <Engine.h>

namespace Maho
{

namespace Physics
{

/** Physics simulation library extension (rigid body solver). Pre-app toolkit (driven by EToolStage). */
class MAHO_PHYSICS_API FPhysics final : public TExtension<EToolStage, FPhysics>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FPhysics>;
	FPhysics() = default;
};

} // namespace Physics

} // namespace Maho
