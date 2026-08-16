#include <RHI.h>

namespace Maho::RHI
{

bool FRHI::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = Initialize() (start RHI thread); Shutdown = Shutdown().
	(void)Stage;
	return true;
}

const char* FRHI::GetThreadName() const
{
	return "RHI";
}

} // namespace Maho::RHI

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FRHIAdapter final : public Maho::IExtension<Maho::EEngineStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EEngineStage Stage) override
	{
		return Maho::RHI::FRHI::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_RHI_API Maho::IExtension<Maho::EEngineStage>* CreateExtension()
{
	return new FRHIAdapter();
}
