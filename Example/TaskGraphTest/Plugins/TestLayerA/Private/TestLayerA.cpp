#include "TestLayerA.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Maho
{
namespace
{
	// ---- tunables (env, so a stress run needs no rebuild) --------------------

	std::uint64_t EnvU64(const char* Name, std::uint64_t Fallback)
	{
		const char* Raw = std::getenv(Name);
		if (Raw == nullptr)
		{
			return Fallback;
		}
		const long long Parsed = std::strtoll(Raw, nullptr, 10);
		return Parsed > 0 ? static_cast<std::uint64_t>(Parsed) : Fallback;
	}

	std::uint64_t FramesToRun() { return EnvU64("MAHO_TEST_FRAMES", 30); }
	std::chrono::microseconds TickWork() { return std::chrono::microseconds(EnvU64("MAHO_TEST_WORK_US", 2000)); }

	// ---- cross-frame exclusivity probe --------------------------------------
	//
	// INVARIANT: consecutive frames of the SAME stage never overlap. Today that is
	// FGroupGate's job (per layer); after the refactor the implicit cross-frame
	// self-edge must keep it per node. One counter per mounted stage:
	// 0 = IInit, 1 = ITick, 2 = IShutdown.

	std::atomic<int> GInStage[3];
	std::atomic<int> GViolations{ 0 };

	struct FStageProbe
	{
		std::atomic<int>& Counter;
		const char*       Label;

		FStageProbe(std::atomic<int>& InCounter, const char* InLabel)
			: Counter(InCounter), Label(InLabel)
		{
			if (Counter.fetch_add(1, std::memory_order_acq_rel) != 0)
			{
				GViolations.fetch_add(1, std::memory_order_relaxed);
				std::printf("[A] !! OVERLAP in %s (two instances of the same node ran at once)\n", Label);
				std::fflush(stdout);
			}
		}

		~FStageProbe() { Counter.fetch_sub(1, std::memory_order_acq_rel); }
	};

	// ---- service availability (proves the Common layers were actually driven) --

	constexpr int    kServiceCount = 7;
	std::atomic<int> GServicesUp{ 0 };

	// ---- thread census (serial = 1, parallel = many) ------------------------

	std::mutex                 GThreadMutex;
	std::vector<std::uint64_t> GThreads;

	void NoteThread()
	{
		const std::uint64_t Id = static_cast<std::uint64_t>(
			std::hash<std::thread::id>{}(std::this_thread::get_id()));

		std::lock_guard<std::mutex> Lock(GThreadMutex);
		for (std::uint64_t Seen : GThreads)
		{
			if (Seen == Id) { return; }
		}
		if (GThreads.size() < 256) { GThreads.push_back(Id); }
	}

	std::size_t ThreadCount()
	{
		std::lock_guard<std::mutex> Lock(GThreadMutex);
		return GThreads.size();
	}
}

FTestLayerA::FTestLayerA()
{
	// Real cross-layer edges: my IInit waits for each service layer's IInit. These are
	// the declarations the scheduler has to resolve -- exactly the shape the bridge must
	// keep working across the refactor.
	MyStage<IInit>().IsWaiting<Config::FConfig>().ForStage<IInit>();
	MyStage<IInit>().IsWaiting<Paths::FPaths>().ForStage<IInit>();
	MyStage<IInit>().IsWaiting<Name::FNamePool>().ForStage<IInit>();
	MyStage<IInit>().IsWaiting<Timer::FTimer>().ForStage<IInit>();
	MyStage<IInit>().IsWaiting<Text::FTextManager>().ForStage<IInit>();
	MyStage<IInit>().IsWaiting<Exception::FException>().ForStage<IInit>();
	MyStage<IInit>().IsWaiting<FLog>().ForStage<IInit>();
}

void FTestLayerA::Initialize(FEngineBase&, FEngineContext&)
{
	FStageProbe Probe(GInStage[0], "Init");
	NoteThread();

	// A null accessor means that layer was NOT driven. The install tree only proves a
	// layer was installed; this is the observable form of "silently not scheduled" --
	// the failure mode where a layer ends up in a collector whose stage set it does not
	// mount, so no node is generated and its declarations are never even validated.
	struct FService { const char* Name; bool bUp; };
	const FService Services[kServiceCount] = {
		{ "Config",    Config::GetConfig() != nullptr },
		{ "Paths",     Paths::GetPaths() != nullptr },
		{ "Name",      Name::GetNamePool() != nullptr },
		{ "Timer",     Timer::GetTimer() != nullptr },
		{ "Text",      Text::GetTextManager() != nullptr },
		{ "Exception", Exception::GetExceptionCenter() != nullptr },
		{ "Log",       GetLog() != nullptr },
	};

	int Up = 0;
	for (const FService& S : Services)
	{
		if (S.bUp) { ++Up; }
		else
		{
			std::printf("[A] !! SERVICE DOWN: %s (layer was not driven?)\n", S.Name);
			std::fflush(stdout);
		}
	}
	GServicesUp.store(Up);

	std::printf("[A] init      servicesUp=%d/%d\n", Up, kServiceCount);
	std::fflush(stdout);
}

void FTestLayerA::Tick(FEngineBase& Engine, FEngineContext& Frame)
{
	FStageProbe Probe(GInStage[1], "Tick");
	NoteThread();

	++TickCount;
	std::this_thread::sleep_for(TickWork());

	if (TickCount <= 3 || (TickCount % 25) == 0)
	{
		std::printf("[A] tick %llu\n", static_cast<unsigned long long>(TickCount));
		std::fflush(stdout);
	}

	if (TickCount >= FramesToRun())
	{
		Engine.RequestExit();
	}
}

void FTestLayerA::Shutdown(FEngineBase&, FEngineContext&)
{
	FStageProbe Probe(GInStage[2], "Shutdown");

	std::printf("[A] SUMMARY ticks=%llu threads=%llu crossFrameOverlaps=%d servicesUp=%d/%d frames=%llu workUs=%llu\n",
		static_cast<unsigned long long>(TickCount),
		static_cast<unsigned long long>(ThreadCount()),
		GViolations.load(),
		GServicesUp.load(),
		kServiceCount,
		static_cast<unsigned long long>(FramesToRun()),
		static_cast<unsigned long long>(TickWork().count()));
	std::fflush(stdout);
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_TESTLAYERA_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FTestLayerA::CreateFrame();
}
