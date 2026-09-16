#include "TestLayerB.h"

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

	/** Frames to run: both A and B read this and request exit at the same count. */
	std::uint64_t FramesToRun()
	{
		return EnvU64("MAHO_TEST_FRAMES", 30);
	}

	/** Per-tick work, in microseconds. Widens the window so overlap is observable. */
	std::chrono::microseconds TickWork()
	{
		return std::chrono::microseconds(EnvU64("MAHO_TEST_WORK_US", 2000));
	}

	// ---- cross-frame exclusivity probe --------------------------------------
	//
	// INVARIANT: consecutive frames of the SAME stage never overlap. Today that is
	// FGroupGate's job (per layer); after the refactor the implicit cross-frame
	// self-edge must keep it per node. A violation = two instances of one node ran at
	// once, i.e. the exact failure the gate/self-edge exists to prevent.
	// One counter per mounted stage (0 = IInit, 1 = ITick, 2 = IShutdown).

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
				std::printf("[B] !! OVERLAP in %s (two instances of the same node ran at once)\n", Label);
				std::fflush(stdout);
			}
		}

		~FStageProbe()
		{
			Counter.fetch_sub(1, std::memory_order_acq_rel);
		}
	};

	// ---- thread census (serial = 1, parallel = many) ------------------------

	std::mutex             GThreadMutex;
	std::vector<std::uint64_t> GThreads;

	void NoteThread()
	{
		const std::uint64_t Id = static_cast<std::uint64_t>(
			std::hash<std::thread::id>{}(std::this_thread::get_id()));

		std::lock_guard<std::mutex> Lock(GThreadMutex);
		for (std::uint64_t Seen : GThreads)
		{
			if (Seen == Id)
			{
				return;
			}
		}
		if (GThreads.size() < 256)
		{
			GThreads.push_back(Id);
		}
	}

	std::size_t ThreadCount()
	{
		std::lock_guard<std::mutex> Lock(GThreadMutex);
		return GThreads.size();
	}
}

void FTestLayerB::Initialize(FEngineBase&)
{
	FStageProbe Probe(GInStage[0], "Init");
	NoteThread();
	std::printf("[B] init\n");
	std::fflush(stdout);
}

void FTestLayerB::Tick(FEngineBase& Engine)
{
	FStageProbe Probe(GInStage[1], "Tick");
	NoteThread();

	++TickCount;

	// Burn a tunable slice so this frame's Tick overlaps a lot of other work.
	std::this_thread::sleep_for(TickWork());

	if (TickCount <= 3 || (TickCount % 25) == 0)
	{
		std::printf("[B] tick %llu\n", static_cast<unsigned long long>(TickCount));
		std::fflush(stdout);
	}

	if (TickCount >= FramesToRun())
	{
		Engine.RequestExit();
	}
}

void FTestLayerB::Shutdown(FEngineBase&)
{
	FStageProbe Probe(GInStage[2], "Shutdown");

	// Verdict line: a stress run is judged from this.
	std::printf("[B] SUMMARY ticks=%llu threads=%llu crossFrameOverlaps=%d frames=%llu workUs=%llu\n",
		static_cast<unsigned long long>(TickCount),
		static_cast<unsigned long long>(ThreadCount()),
		GViolations.load(),
		static_cast<unsigned long long>(FramesToRun()),
		static_cast<unsigned long long>(TickWork().count()));
	std::fflush(stdout);
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_TESTLAYERB_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FTestLayerB::CreateFrame();
}
