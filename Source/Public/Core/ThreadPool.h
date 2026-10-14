#pragma once


#include <Core/Export.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace Maho
{

class MAHO_API FThreadPool
{
public:
	using FLane = std::uint32_t;

	static constexpr FLane         DefaultLane = 0;
	static constexpr FLane         InvalidLane = 0xFFFFFFFFu;
	static constexpr std::uint32_t MaxLanes    = 16;

	explicit FThreadPool(std::uint32_t NumThreads = 0);
	~FThreadPool();

	FThreadPool(const FThreadPool&) = delete;
	FThreadPool& operator=(const FThreadPool&) = delete;

	// A lane is one OWNER's queue -- one per layer that submits its own work (the engine host, a
	// graph), so a barrier can drain exactly that owner's tasks. There is deliberately no lane-less
	// spelling of Submit/Flush: "whose work is this?" is the question the barrier exists to answer,
	// and a default lane answers it for everyone but DefaultLane's owner.
	[[nodiscard]] FLane CreateLane();

	void DestroyLane(FLane Lane);

	// A PERSISTENT lane is a lane with its own dedicated thread: that thread consumes ONLY this lane,
	// and the shared workers never take from it. This is what THREAD AFFINITY needs -- a module that
	// owns an OS handle (a Win32 window's message queue lives on the thread that created it) cannot
	// have its work run by whichever worker happens to be free (see AGENTS "Thread Affinity"). It is
	// the pool-backed form of "this layer owns a resident thread", and it replaces the per-layer
	// std::thread + queue that FThreadedServer used to carry.
	//
	// `ThreadName` is only a label (traces and debuggers); the lane must be released with
	// DestroyPersistentLane, whose thread drains the lane before it exits.
	[[nodiscard]] FLane CreatePersistentLane(const char* ThreadName);

	void DestroyPersistentLane(FLane Lane);

	// True when the CALLING thread is the one pinned to Lane (persistent lanes only). This replaces a
	// server's private thread-id check: a module can ask "am I on my own thread?" without owning one.
	[[nodiscard]] bool IsCurrentThread(FLane Lane) const;

	// Enqueue onto Lane and return immediately. DefaultLane exists from construction.
	void Submit(FLane Lane, std::function<void()> Task);

	// Quiescence barrier for ONE lane: block until that lane's queue is empty and nothing is running
	// from it. Tolerates concurrent Submit -- a nested graph may add work while we wait.
	void Flush(FLane Lane);

	[[nodiscard]] std::uint32_t GetNumThreads() const;

	[[nodiscard]] std::uint32_t GetLanePending(FLane Lane) const;

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

}
