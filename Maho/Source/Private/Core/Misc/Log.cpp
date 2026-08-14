#include <Core/Misc/Log.h>

#include <Core/EngineBase.h>
#include <Core/Misc/Console.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Maho
{

namespace
{

constexpr const char* GLogPattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v";
constexpr const char* GEditorLogPattern = "[%H:%M:%S.%e] [%n] [%l] %v";
constexpr std::size_t GMaxCapturedLines = 2000;

static TAutoConsoleVariable GCVarLogMaxFileBytes(
	"log.MaxFileBytes",
	5 * 1024 * 1024,
	"Rotating log file max size in bytes");

static TAutoConsoleVariable GCVarLogMaxFileCount(
	"log.MaxFileCount",
	3,
	"Rotating log file count");

std::shared_ptr<spdlog::logger> CreateLogger(
	const std::string& Name,
	const std::vector<spdlog::sink_ptr>& Sinks)
{
	if (auto Existing = spdlog::get(Name))
	{
		spdlog::drop(Name);
	}

	auto Logger = std::make_shared<spdlog::logger>(Name, Sinks.begin(), Sinks.end());
	Logger->set_pattern(GLogPattern);
	Logger->set_level(spdlog::level::trace);
	Logger->flush_on(spdlog::level::warn);
	spdlog::register_logger(Logger);
	return Logger;
}

void StripTrailingNewlines(std::string& Text)
{
	while (!Text.empty() && (Text.back() == '\n' || Text.back() == '\r'))
	{
		Text.pop_back();
	}
}

} // namespace

struct FLog::FCaptureState
{
	std::mutex Mutex;
	std::deque<FCapturedLogLine> Lines;
	spdlog::pattern_formatter Formatter;

	FCaptureState()
		: Formatter(std::string(GEditorLogPattern), spdlog::pattern_time_type::local, std::string())
	{
	}
};

FLog::~FLog()
{
	Shutdown();
	Capture.reset();
}

void FLog::Initialize(const FLogConfig& Config)
{
	if (bInitialized)
	{
		Shutdown();
	}

	std::vector<spdlog::sink_ptr> Sinks;

	if (Config.bEnableConsole)
	{
		auto ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		ConsoleSink->set_level(spdlog::level::trace);
		Sinks.push_back(ConsoleSink);
	}

	if (Config.bEnableFile)
	{
		namespace fs = std::filesystem;
		std::error_code ErrorCode;
		fs::create_directories(Config.LogDirectory, ErrorCode);

		const std::size_t MaxFileBytes = static_cast<std::size_t>(
			(std::max)(1024, GCVarLogMaxFileBytes.GetValue()));
		const std::size_t MaxFileCount = static_cast<std::size_t>(
			(std::max)(1, GCVarLogMaxFileCount.GetValue()));

		const fs::path LogFilePath = fs::path(Config.LogDirectory) / (Config.CoreLoggerName + ".log");
		auto FileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
			LogFilePath.string(),
			MaxFileBytes,
			MaxFileCount);
		FileSink->set_level(spdlog::level::trace);
		Sinks.push_back(FileSink);
	}

	if (Config.bEnableEditorCapture)
	{
		if (!Capture)
		{
			Capture = std::make_shared<FCaptureState>();
		}

		std::weak_ptr<FCaptureState> WeakCapture = Capture;
		auto EditorSink = std::make_shared<spdlog::sinks::callback_sink_mt>(
			[WeakCapture](const spdlog::details::log_msg& Msg)
			{
				std::shared_ptr<FCaptureState> State = WeakCapture.lock();
				if (!State)
				{
					return;
				}

				std::lock_guard<std::mutex> Lock(State->Mutex);

				spdlog::memory_buf_t Formatted;
				State->Formatter.format(Msg, Formatted);

				FCapturedLogLine Line;
				Line.Level = Msg.level;
				Line.Text = SPDLOG_BUF_TO_STRING(Formatted);
				StripTrailingNewlines(Line.Text);

				State->Lines.push_back(std::move(Line));
				while (State->Lines.size() > GMaxCapturedLines)
				{
					State->Lines.pop_front();
				}
			});
		EditorSink->set_level(spdlog::level::trace);
		Sinks.push_back(EditorSink);
	}

	if (Sinks.empty())
	{
		Sinks.push_back(std::make_shared<spdlog::sinks::null_sink_mt>());
	}

	CoreLogger = CreateLogger(Config.CoreLoggerName, Sinks);
	ClientLogger = CreateLogger(Config.ClientLoggerName, Sinks);
	spdlog::set_default_logger(CoreLogger);

	bInitialized = true;
	CoreLogger->info(
		"Logging initialized (console={}, file={}, editor={})",
		Config.bEnableConsole,
		Config.bEnableFile,
		Config.bEnableEditorCapture);
}

void FLog::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	if (CoreLogger)
	{
		CoreLogger->info("Logging shutdown");
		CoreLogger->flush();
		spdlog::drop(CoreLogger->name());
		CoreLogger.reset();
	}
	if (ClientLogger)
	{
		ClientLogger->flush();
		spdlog::drop(ClientLogger->name());
		ClientLogger.reset();
	}

	bInitialized = false;
}

void FLog::DrainCapturedLines(std::vector<FCapturedLogLine>& Out)
{
	Out.clear();
	if (!Capture)
	{
		return;
	}

	std::lock_guard<std::mutex> Lock(Capture->Mutex);
	if (Capture->Lines.empty())
	{
		return;
	}

	Out.reserve(Capture->Lines.size());
	for (FCapturedLogLine& Line : Capture->Lines)
	{
		Out.push_back(std::move(Line));
	}
	Capture->Lines.clear();
}

std::shared_ptr<spdlog::logger>& FLog::GetActiveCoreLogger()
{
	FLog& Log = GEngine->GetLog();
	if (!Log.CoreLogger)
	{
		throw std::runtime_error("FLog: core logger not initialized (FEngineBase::Initialize)");
	}
	return Log.CoreLogger;
}

std::shared_ptr<spdlog::logger>& FLog::GetActiveClientLogger()
{
	FLog& Log = GEngine->GetLog();
	if (!Log.ClientLogger)
	{
		throw std::runtime_error("FLog: client logger not initialized (FEngineBase::Initialize)");
	}
	return Log.ClientLogger;
}

} // namespace Maho
