#include <Physics.h>

namespace Maho::Physics
{

bool FPhysics::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = create solver world; Shutdown = destroy.
	(void)Stage;
	return true;
}

} // namespace Maho::Physics

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FPhysicsAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Physics::FPhysics::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_PHYSICS_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FPhysicsAdapter();
}
