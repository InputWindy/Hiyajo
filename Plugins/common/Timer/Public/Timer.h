#pragma once

// Timer - profiling scope timing (engine Common, engine layer). FScopedTimer is
// RAII scope timing around FTimer's hierarchical scope profiler.
#include <Maho.h>

#include "TimerApi.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace Maho
{
namespace Timer
{

class FTimer;

/** Global timer accessor - returns FTimer* (cross-DLL via function). */
MAHO_TIMER_API FTimer* GetTimer();

/**
 * Stack-based scope profiler - hierarchical timing instrumentation.
 *
 *   void Render()
 *   {
 *       Timer::FScopedTimer Scope("Render");   // BeginScope on construction
 *       // ... work ...
 *   }                                                // EndScope on destruction
 *
 *   Timer::GetTimer()->DumpToString();   // "Render: 1.23 ms (n calls, avg, max)"
 */
class FTimer
	: public FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>
{
public:
	MAHO_DECLARE_LAYER(FTimer, "Timer.dll");

	/** Push a scope (must be balanced with EndScope / FScopedTimer). */
	void BeginScope(std::string_view Name);

	/** Pop the current scope and accumulate its elapsed time. */
	void EndScope();

	/** Clear all accumulated timings. */
	void Reset();

	/** Format the timing tree as text (milliseconds). */
	[[nodiscard]] std::string DumpToString() const;

private:
	// -- engine pipeline stages (scheduler-only) --
	void PreInitialize(FEngineBase&) override {}
	void Initialize(FEngineBase& Engine) override;
	void PostInitialize(FEngineBase&) override {}
	void PreShutdown(FEngineBase&) override {}
	void Shutdown(FEngineBase& Engine) override;
	void PostShutdown(FEngineBase&) override {}

protected:
	FTimer() = default;

	struct FNode
	{
		std::string Name;
		double TotalSeconds = 0.0;
		double MaxSeconds = 0.0;
		std::uint32_t Count = 0;
		std::chrono::steady_clock::time_point Start;
		FNode* Parent = nullptr;
		std::map<std::string, FNode> Children;
	};

	FNode Root{"Root"};
	FNode* Current = &Root;
};

/** RAII scope timer - BeginScope on construction, EndScope on destruction. */
class FScopedTimer
{
public:
	explicit FScopedTimer(std::string_view Name);
	~FScopedTimer();

	FScopedTimer(const FScopedTimer&) = delete;
	FScopedTimer& operator=(const FScopedTimer&) = delete;
};

} // namespace Timer
} // namespace Maho
