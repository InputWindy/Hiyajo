#pragma once

// Timer — profiling + game clock (engine Common, TSingleton). FScopedTimer is
// RAII scope timing; FGameClock is a lazily-advanced real/game clock with
// time-scale and pause.
#include <Core/Singleton.h>
#include <Engine/Layer.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace Maho
{
namespace Timer
{

/**
 * Stack-based scope profiler — hierarchical timing instrumentation.
 *
 *   void Render()
 *   {
 *       Timer::FScopedTimer Scope("Render");   // BeginScope on construction
 *       // ... work ...
 *   }                                                // EndScope on destruction
 *
 *   Timer::FTimer::Get().DumpToString();   // "Render: 1.23 ms (n calls, avg, max)"
 */
class FTimer
	: public TSingleton<FTimer>
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Process-unique accessor — defined in Timer.cpp (in Timer.dll). */
	static FTimer& Get();

	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;

	/** Push a scope (must be balanced with EndScope / FScopedTimer). */
	void BeginScope(std::string_view Name);

	/** Pop the current scope and accumulate its elapsed time. */
	void EndScope();

	/** Clear all accumulated timings. */
	void Reset();

	/** Format the timing tree as text (milliseconds). */
	[[nodiscard]] std::string DumpToString() const;

protected:
	friend TSingleton<FTimer>;
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

/** RAII scope timer — BeginScope on construction, EndScope on destruction. */
class FScopedTimer
{
public:
	explicit FScopedTimer(std::string_view Name);
	~FScopedTimer();

	FScopedTimer(const FScopedTimer&) = delete;
	FScopedTimer& operator=(const FScopedTimer&) = delete;
};

/**
 * Game clock — real/game time with time scale and pause. Lazily advanced:
 * GetGameSeconds() accumulates wall-clock delta × TimeScale since the last
 * advance, so no per-frame Tick is required.
 */
class FGameClock
	: public TSingleton<FGameClock>
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Process-unique accessor — defined in Timer.cpp (in Timer.dll). */
	static FGameClock& Get();

	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;

	/** Seconds since construction (wall clock). */
	[[nodiscard]] double GetRealSeconds() const;

	/** Game seconds (scaled, frozen while paused). Lazily advanced. */
	[[nodiscard]] double GetGameSeconds();

	/** Game-time delta since the previous advance, in seconds. */
	[[nodiscard]] double GetDeltaSeconds();

	void SetTimeScale(double InScale);
	void SetPaused(bool bPaused);
	[[nodiscard]] double GetTimeScale() const;
	[[nodiscard]] bool IsPaused() const;

protected:
	friend TSingleton<FGameClock>;
	FGameClock() = default;

	double GameSeconds = 0.0;
	double DeltaSeconds = 0.0;
	double TimeScale = 1.0;
	bool bPaused = false;
	std::chrono::steady_clock::time_point InitTime{std::chrono::steady_clock::now()};
	std::chrono::steady_clock::time_point LastReal{std::chrono::steady_clock::now()};
};

} // namespace Timer
} // namespace Maho
