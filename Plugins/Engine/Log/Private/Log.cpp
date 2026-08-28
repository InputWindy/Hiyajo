// Log is an FEngineLayer. Initialize brings the spdlog logger up + publishes
// `this` via GetLog(); Shutdown flushes + drops it. The spdlog type stays in
// this TU — the header only sees the forward declaration.
#include "Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace Maho
{

FLog* GLog = nullptr;

MAHO_LOG_API FLog* GetLog()
{
	return GLog;
}

FLog::FLog() = default;

FLog::~FLog() = default;   // 完整类型 spdlog::logger 在此可见

void FLog::Initialize(FEngineBase& Engine)
{
	// stdout (color) + rotating file — GUI apps (WIN32 subsystem) have no
	// console, so the file sink is the durable log destination.
	auto ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	auto FileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
		"Logs/Maho.log", 1024 * 1024 * 5, 3);

	Logger = std::make_shared<spdlog::logger>("Maho",
		spdlog::sinks_init_list{ ConsoleSink, FileSink });
	spdlog::register_logger(Logger);

	Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

	spdlog::level::level_enum Lv = spdlog::level::debug;
	// 从引擎启动参数取 --log-level=trace|debug|info|warn|error
	char** Argv = Engine.GetLaunchArgv();
	if (Argv)
	{
		for (int i = 0; Argv[i]; ++i)
		{
			if (const char* V = Argv[i][0] == '-' ? Argv[i] + 1 : nullptr;
				V && std::string_view(V) == "log-level" && Argv[i + 1])
			{
				Lv = spdlog::level::from_str(Argv[i + 1]);
			}
		}
	}
	Logger->set_level(Lv);

	GLog = this;
}

void FLog::Shutdown(FEngineBase&)
{
	GLog = nullptr;
	spdlog::shutdown();
	Logger.reset();
}

void FLog::LogLine(ELogLevel Level, std::string Message)
{
	if (!Logger)
	{
		return;
	}
	switch (Level)
	{
	case ELogLevel::Trace:    Logger->trace(std::move(Message)); break;
	case ELogLevel::Debug:    Logger->debug(std::move(Message)); break;
	case ELogLevel::Info:     Logger->info(std::move(Message)); break;
	case ELogLevel::Warn:     Logger->warn(std::move(Message)); break;
	case ELogLevel::Error:    Logger->error(std::move(Message)); break;
	case ELogLevel::Critical: Logger->critical(std::move(Message)); break;
	}
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_LOG_API Maho::FEngineLayer* CreateLayer()
{
	return Maho::FLog::CreateLayer();
}
