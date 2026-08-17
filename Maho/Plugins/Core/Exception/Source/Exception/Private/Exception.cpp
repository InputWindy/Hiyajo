#include <Exception.h>

namespace Maho::Exception
{

bool FException::ExecuteStage(EEngineStage Stage)
{
	if (Stage == EEngineStage::Init || Stage == EEngineStage::Shutdown)
	{
		OnException.RemoveAll();
	}
	return true;
}

void FException::ReportException(std::string_view Message)
{
	OnException.Broadcast(std::string(Message));
}

void FException::ReportException(const std::exception& Error)
{
	ReportException(Error.what());
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
