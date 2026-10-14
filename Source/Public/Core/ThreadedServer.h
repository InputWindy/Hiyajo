#pragma once


#include <Core/Export.h>

#include <functional>
#include <memory>

namespace Maho
{

class FThreadPool;

// A RESIDENT role: one lane of the pool, served by a thread of its own, FIFO, drained by Flush.
//
// The thread is the point. A module that owns an OS handle -- a Win32 window's message queue belongs
// to the thread that created it -- cannot have its work run by whichever worker happens to be free
// (see AGENTS "Thread Affinity"), so its lane is PERSISTENT: `FThreadPool::CreatePersistentLane`
// gives it a dedicated thread that serves that lane and nothing else. What this class adds on top is
// the role's lifecycle (OnInitialize/OnShutdown), its name for traces, and "am I on my own thread?"
//
// The pool is INJECTED: a server is a collector's sibling, so it wants the HOST's worker set, not one
// of its own -- `UseSharedPool(Engine.GetPool())` before Initialize, exactly the shape
// `FFrameBuilder::UseSharedPool` already has. Without it the server owns a pool (standalone use).
class MAHO_API FThreadedServer
{
public:
	FThreadedServer();
	virtual ~FThreadedServer();

	FThreadedServer(const FThreadedServer&) = delete;
	FThreadedServer& operator=(const FThreadedServer&) = delete;

	/** Live on this pool's lane instead of one of our own. Must be called before Initialize. */
	void UseSharedPool(FThreadPool& InPool);

	/** Starts the resident lane (named `GetThreadName()`); then OnInitialize's work can be queued.
	 *  Returns false when the lane could not be obtained (the pool is at lane capacity). */
	bool Initialize();

	/** Drains the lane, releases it (its thread exits after the queue is empty), then OnShutdown. */
	void Shutdown();

	[[nodiscard]] bool IsRunning() const;

	/** Queue work for this role's thread. Refused (and reported) unless Initialize has run. */
	void Submit(std::function<void()> Task);

	/** Block until the lane has drained -- the role's own quiescence point. */
	void Flush();

	/** True when the CALLING thread is this role's thread. */
	[[nodiscard]] bool IsServerThread() const;

protected:
	[[nodiscard]] virtual bool OnInitialize();

	virtual void OnShutdown();

	/** The lane's name: what a trace calls this role's row, and what the lane is registered as. */
	[[nodiscard]] virtual const char* GetThreadName() const;

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

}
