#include <Exception.h>

namespace Maho::Exception
{

bool FException::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = register exception handlers; Shutdown = unregister.
	(void)Stage;
	return true;
}

} // namespace Maho::Exception

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FExceptionAdapter final : public Maho::IExtension<Maho::EEngineStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EEngineStage Stage) override
	{
		return Maho::Exception::FException::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_EXCEPTION_API Maho::IExtension<Maho::EEngineStage>* CreateExtension()
{
	return new FExceptionAdapter();
}
