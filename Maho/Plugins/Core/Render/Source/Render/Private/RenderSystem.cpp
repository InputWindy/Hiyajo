#include <RenderSystem.h>

namespace Maho::Render
{

bool FRenderSystem::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = Initialize() (start render thread); Shutdown = Shutdown().
	(void)Stage;
	return true;
}

const char* FRenderSystem::GetThreadName() const
{
	return "Render";
}

} // namespace Maho::Render

// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FRenderSystemAdapter final : public Maho::IExtension<Maho::EEngineStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EEngineStage Stage) override
	{
		return Maho::Render::FRenderSystem::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_RENDER_API Maho::IExtension<Maho::EEngineStage>* CreateExtension()
{
	return new FRenderSystemAdapter();
}
