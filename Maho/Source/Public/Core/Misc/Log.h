#pragma once

#include <Core/Misc/Export.h>

#include <memory>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#	pragma warning(push)
#	pragma warning(disable : 4459) // fmt: 'formattable' hides global
#endif
#include <spdlog/spdlog.h>
#if defined(_MSC_VER)
#	pragma warning(pop)
#endif

namespace Maho
{

struct FLogConfig
{
	/** Core (engine) logger name. */
	std::string CoreLoggerName = "Maho";

	/** Client (game) logger name — typically ApplicationName. */
	std::string ClientLoggerName = "App";

	/** Directory for log files (UE-style: Saved/Logs). */
	std::string LogDirectory = "Saved/Logs";

	/** OS console (stdout). Off by default for GUI apps; Output Log captures instead. */
	bool bEnableConsole = false;

	bool bEnableFile = true;

	/** Ring buffer for the editor Output Log panel. */
	bool bEnableEditorCapture = true;
};

/** One captured line for the in-editor Output Log (thread-safe drain). */
struct FCapturedLogLine
{
	spdlog::level::level_enum Level = spdlog::level::info;
	std::string Text;
};

/**
 * App-owned spdlog facade. MAHO_* macros resolve via GEngine->GetLog().
 */
class MAHO_API FLog
{
public:
	FLog() = default;
	~FLog();

	FLog(const FLog&) = delete;
	FLog& operator=(const FLog&) = delete;

	/** Create / replace this instance's loggers. Safe to call again. */
	void Initialize(const FLogConfig& Config);
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }

	[[nodiscard]] std::shared_ptr<spdlog::logger>& GetCoreLogger() { return CoreLogger; }
	[[nodiscard]] std::shared_ptr<spdlog::logger>& GetClientLogger() { return ClientLogger; }

	/**
	 * Move pending editor-capture lines into Out (clears the ring).
	 * Safe to call from the UI thread every frame.
	 */
	void DrainCapturedLines(std::vector<FCapturedLogLine>& Out);

	/** Macro helpers — forward to GEngine->GetLog(). */
	[[nodiscard]] static std::shared_ptr<spdlog::logger>& GetActiveCoreLogger();
	[[nodiscard]] static std::shared_ptr<spdlog::logger>& GetActiveClientLogger();

private:
	struct FCaptureState;

	std::shared_ptr<spdlog::logger> CoreLogger;
	std::shared_ptr<spdlog::logger> ClientLogger;
	std::shared_ptr<FCaptureState> Capture;
	bool bInitialized = false;
};

} // namespace Maho

#define MAHO_CORE_TRACE(...)    ::Maho::FLog::GetActiveCoreLogger()->trace(__VA_ARGS__)
#define MAHO_CORE_DEBUG(...)    ::Maho::FLog::GetActiveCoreLogger()->debug(__VA_ARGS__)
#define MAHO_CORE_INFO(...)     ::Maho::FLog::GetActiveCoreLogger()->info(__VA_ARGS__)
#define MAHO_CORE_WARN(...)     ::Maho::FLog::GetActiveCoreLogger()->warn(__VA_ARGS__)
#define MAHO_CORE_ERROR(...)    ::Maho::FLog::GetActiveCoreLogger()->error(__VA_ARGS__)
#define MAHO_CORE_CRITICAL(...) ::Maho::FLog::GetActiveCoreLogger()->critical(__VA_ARGS__)

#define MAHO_TRACE(...)         ::Maho::FLog::GetActiveClientLogger()->trace(__VA_ARGS__)
#define MAHO_DEBUG(...)         ::Maho::FLog::GetActiveClientLogger()->debug(__VA_ARGS__)
#define MAHO_INFO(...)          ::Maho::FLog::GetActiveClientLogger()->info(__VA_ARGS__)
#define MAHO_WARN(...)          ::Maho::FLog::GetActiveClientLogger()->warn(__VA_ARGS__)
#define MAHO_ERROR(...)         ::Maho::FLog::GetActiveClientLogger()->error(__VA_ARGS__)
#define MAHO_CRITICAL(...)      ::Maho::FLog::GetActiveClientLogger()->critical(__VA_ARGS__)
