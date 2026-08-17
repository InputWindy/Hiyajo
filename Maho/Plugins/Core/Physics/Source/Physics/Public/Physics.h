#pragma once

#include "PhysicsApi.h"
#include <Core/Core.h>

namespace Maho
{

namespace Physics
{

/** Physics simulation library extension (rigid body solver). Pre-app toolkit (driven by EToolStage). */
class MAHO_PHYSICS_API FPhysics : public TExtension<EToolStage, FPhysics>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

protected:
	friend TSingleton<FPhysics>;
	FPhysics() = default;
};

} // namespace Physics

} // namespace Maho
