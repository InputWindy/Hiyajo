#include <Log.h>

namespace Maho::Log
{

bool FLogger::ExecuteStage(EToolStage Stage)
{
	switch (Stage)
	{
	case EToolStage::Init:
		spdlog::set_level(spdlog::level::info);
		break;

	case EToolStage::Shutdown:
		spdlog::shutdown();
		break;
	}

	return true;
}

void SetLogLevel(ELogLevel Level)
{
	switch (Level)
	{
	case ELogLevel::Debug:
		spdlog::set_level(spdlog::level::debug);
		break;

	case ELogLevel::Info:
		spdlog::set_level(spdlog::level::info);
		break;

	case ELogLevel::Warn:
		spdlog::set_level(spdlog::level::warn);
		break;

	case ELogLevel::Error:
		spdlog::set_level(spdlog::level::err);
		break;

	case ELogLevel::Off:
		spdlog::set_level(spdlog::level::off);
		break;
	}
}

} // namespace Maho::Log

// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FLoggerAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Log::FLogger::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_LOG_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FLoggerAdapter();
}
