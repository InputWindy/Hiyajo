#include <ResourceSystem.h>

namespace Maho::Resource
{

bool FResourceSystem::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = Initialize() (start async load thread); Shutdown = Shutdown().
	(void)Stage;
	return true;
}

const char* FResourceSystem::GetThreadName() const
{
	return "Resource";
}

} // namespace Maho::Resource

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FResourceSystemAdapter final : public Maho::IExtension<Maho::EEngineStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EEngineStage Stage) override
	{
		return Maho::Resource::FResourceSystem::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_RESOURCE_API Maho::IExtension<Maho::EEngineStage>* CreateExtension()
{
	return new FResourceSystemAdapter();
}
