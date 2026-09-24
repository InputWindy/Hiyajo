// Log is an FEngineLayer. Initialize brings the spdlog logger up + publishes
// `this` via GetLog(); Shutdown flushes + drops it. The spdlog type stays in
// this TU - the header only sees the forward declaration.
#include "Log.h"

#include <Trace.h>
#include <ConsoleVariable.h>

#if MAHO_WITH_LOGGING
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#endif

#include <cstdlib>

namespace Maho
{

/** The CPU profiler's runtime switch. It is DECLARED HERE because Core must not depend on a plugin:
 *  Core exposes `TraceSetEnabled` and this plugin, which already owns the process's log sinks, is the
 *  one place that knows a CVar. MAHO_TRACE still decides the initial value (Core reads it while the
 *  process loads); `r.Trace` overrides it from then on.
 *
 *  Note it is NOT a sink of the engine logger: a trace is tens of megabytes and wants its own file,
 *  not a rotating one -- see Trace.h for where the trace's lines actually go. */
static ConsoleVariable::TAutoConsoleVariable<int> GCVarTrace(
	"r.Trace",
	(std::getenv("MAHO_TRACE") != nullptr) ? 1 : 0,
	"1 = record a CPU trace (same as MAHO_TRACE=1), 0 = off. Takes effect immediately.");

FLog* GLog = nullptr;

MAHO_LOG_API FLog* GetLog()
{
	return GLog;
}

FLog::FLog() = default;

FLog::~FLog() = default;   // full type spdlog::logger is visible here

void FLog::Initialize(FEngineBase& Engine, FEngineContext& Frame)
{
	MAHO_TRACE_STAGE(IInit, "Log init", "bring up the stdout + rotating file sinks");
#if MAHO_WITH_LOGGING
	// stdout (color) + rotating file - GUI apps (WIN32 subsystem) have no
	// console, so the file sink is the durable log destination.
	auto ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	auto FileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
		"Logs/Maho.log", 1024 * 1024 * 5, 3);

	Logger = std::make_shared<spdlog::logger>("Maho",
		spdlog::sinks_init_list{ ConsoleSink, FileSink });
	spdlog::register_logger(Logger);

	Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

	spdlog::level::level_enum Lv = spdlog::level::debug;
	// read --log-level=trace|debug|info|warn|error from the engine command line
	const std::string LogLevel = Engine.Get("log-level");
	if (!LogLevel.empty())
	{
		Lv = spdlog::level::from_str(LogLevel);
	}
	Logger->set_level(Lv);
#else
	// SHIPPING: no logger, no sinks, no log file. `GetLog()` is published anyway: it is the engine's
	// "this layer was driven" observable (tools and tests read it that way), and that must not depend
	// on the configuration. What is gone is the work -- see the gated bodies in Log.h.
	(void)Engine;
	(void)Frame;
#endif // MAHO_WITH_LOGGING

	GLog = this;
}

void FLog::Tick(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(ITick, "Log tick", "apply the trace CVar");
	// Polled, not subscribed: the CVar system has no change callback, so this stage is what makes
	// `r.Trace` take effect without a restart. A load, a compare, and -- only on a real change -- one
	// relaxed store inside Core's switch; on a quiet frame it is the load.
	//
	// The FIRST tick must not turn OFF what MAHO_TRACE asked for: the CVar's value is not a reliable
	// statement about the environment's wish (a 0 there means "this CVar has nothing to say"), and a
	// run started with the env var must record from its very first event. From the second tick on the
	// CVar is authoritative, so `r.Trace 0` really stops the recording.
	const int Requested = GCVarTrace.GetValue();
	if (Requested != LastTraceRequest)
	{
		const bool bFirstTick = (LastTraceRequest < 0);
		LastTraceRequest = Requested;
		if (!bFirstTick || Requested != 0)
		{
			TraceSetEnabled(Requested != 0);
		}
	}
}

void FLog::Shutdown(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IShutdown, "Log shutdown", "flush the trace file and stop spdlog");
	GLog = nullptr;
	// The trace file is one of this plugin's own diagnostics, so the OWNER closes it: events are
	// written straight through as they close, and this flushes what stdio still holds. It must run
	// BEFORE spdlog::shutdown() -- a teardown that logs must never find a dead logger, and the trace
	// is flushed while this module is still fully alive. Costs nothing when MAHO_TRACE was never set.
	TraceFlush();
	// Subscribers (e.g. the Editor Console) unbind themselves in their own Shutdown,
	// so this strand should already be empty. RemoveAll is a safety net only; it drops
	// any remaining subscriptions. This now relies on each module's self-consistent
	// teardown (register → unregister pairing), not on leaking the storage to dodge a
	// cross-DLL destructor.
	OnLog.RemoveAll();
#if MAHO_WITH_LOGGING
	spdlog::shutdown();
	Logger.reset();
#endif
}

void FLog::LogLine(ELogLevel Level, std::string Message)
{
	LogLine(Level, "", std::move(Message));
}

void FLog::LogLine(ELogLevel Level, std::string Category, std::string Message)
{
#if MAHO_WITH_LOGGING
	// Deliver to live listeners unconditionally (independent of spdlog state),
	// then forward to the sink if the logger is up.
	const FLogMessage Msg{ Level, std::move(Category), std::move(Message) };
	OnLog.Broadcast(Msg);

	if (!Logger)
	{
		return;
	}
	switch (Level)
	{
	case ELogLevel::Trace:    Logger->trace(Msg.Message); break;
	case ELogLevel::Debug:    Logger->debug(Msg.Message); break;
	case ELogLevel::Info:     Logger->info(Msg.Message); break;
	case ELogLevel::Warn:     Logger->warn(Msg.Message); break;
	case ELogLevel::Error:    Logger->error(Msg.Message); break;
	case ELogLevel::Critical: Logger->critical(Msg.Message); break;
	}
#else
	// SHIPPING: nothing to do -- no subscribers (no editor), no sink. Kept as a definition because
	// the header's passthrough templates still name it in their (now empty) bodies.
	(void)Level;
	(void)Category;
	(void)Message;
#endif // MAHO_WITH_LOGGING
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_LOG_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FLog::CreateFrame();
}
