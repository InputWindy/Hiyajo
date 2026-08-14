#pragma once

#include <Core/Misc/Export.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

/** One named scope's accumulated timing stats. */
struct FTimerScopeSample
{
	std::string Name;
	std::uint64_t CallCount = 0;
	double TotalMilliseconds = 0.0;
	double MinMilliseconds = 0.0;
	double MaxMilliseconds = 0.0;
	double LastMilliseconds = 0.0;

	[[nodiscard]] double AverageMilliseconds() const
	{
		return CallCount > 0 ? (TotalMilliseconds / static_cast<double>(CallCount)) : 0.0;
	}
};

/**
 * Uniform timer query payload (no polymorphism).
 * One package per category name — suitable for log and future UI.
 */
struct MAHO_API FTimerDataPackage
{
	/** Category name passed to MAHO_SCOPED_TIMER, e.g. "Engine" / "Render". */
	std::string SourceName;
	std::vector<FTimerScopeSample> Samples;

	[[nodiscard]] std::string Serialize() const;
};

/**
 * App-owned multi-category timer.
 * MAHO_SCOPED_TIMER resolves via GEngine->GetTimer().
 */
class MAHO_API FTimer
{
public:
	FTimer() = default;
	~FTimer() = default;

	FTimer(const FTimer&) = delete;
	FTimer& operator=(const FTimer&) = delete;

	[[nodiscard]] static FTimer* TryGet();
	[[nodiscard]] static FTimer& Get();

	void Record(const char* CategoryName, const char* ScopeName, double ElapsedMilliseconds);

	void Reset(const char* CategoryName);
	void ResetAll();

	[[nodiscard]] bool TryQuery(const char* CategoryName, FTimerDataPackage& OutPackage) const;
	[[nodiscard]] std::vector<FTimerDataPackage> QueryAll() const;

private:
	struct FScopeAccum
	{
		std::uint64_t CallCount = 0;
		double TotalMilliseconds = 0.0;
		double MinMilliseconds = 0.0;
		double MaxMilliseconds = 0.0;
		double LastMilliseconds = 0.0;
	};

	mutable std::mutex Mutex;
	std::unordered_map<std::string, std::unordered_map<std::string, FScopeAccum>> Categories;
};

/**
 * RAII scope timer. Prefer MAHO_SCOPED_TIMER(CategoryName, ScopeName).
 */
class MAHO_API FScopedTimer
{
public:
	FScopedTimer(const char* CategoryName, const char* ScopeName);
	~FScopedTimer();

	FScopedTimer(const FScopedTimer&) = delete;
	FScopedTimer& operator=(const FScopedTimer&) = delete;

private:
	FTimer* Timer = nullptr;
	const char* CategoryName = nullptr;
	const char* ScopeName = nullptr;
	std::chrono::steady_clock::time_point StartTime{};
	bool bActive = false;
};

} // namespace Maho

#define MAHO_TIMER_CONCAT_INNER(A, B) A##B
#define MAHO_TIMER_CONCAT(A, B) MAHO_TIMER_CONCAT_INNER(A, B)

/**
 * Time the enclosing scope under a category on the App-owned FTimer.
 * Example: MAHO_SCOPED_TIMER("Engine", "FEngineBase::Tick");
 */
#define MAHO_SCOPED_TIMER(CategoryName, ScopeName) \
	::Maho::FScopedTimer MAHO_TIMER_CONCAT(_MahoScopedTimer_, __LINE__)(CategoryName, ScopeName)
