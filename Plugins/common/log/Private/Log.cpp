// Log is an FEngineLayer reached via FLog::Get() (the single instance lives in
// this DLL). Initialize/Shutdown are the IInit/IShutdown stages driven by the
// engine's init/shutdown pipelines.
#include "Log.h"

namespace Maho
{

FLog& FLog::Get()
{
	static FLog Instance;
	return Instance;
}

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
}

void FLog::Shutdown(FEngineBase&)
{
	spdlog::shutdown();
	Logger.reset();
}

} // namespace Maho
