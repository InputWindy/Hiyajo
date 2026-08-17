#include <GameResource.h>

namespace Maho
{

namespace GameResource
{

bool FGameResource::ExecuteStage(EEngineStage Stage)
{
	// TODO: per-stage behavior.
	(void)Stage;
	return true;
}

} // namespace GameResource

} // namespace Maho


// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FGameResourceAdapter final : public Maho::IExtension<Maho::EEngineStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EEngineStage Stage) override
	{
		return Maho::GameResource::FGameResource::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_GAMERESOURCE_API Maho::IExtension<Maho::EEngineStage>* CreateExtension()
{
	return new FGameResourceAdapter();
}
