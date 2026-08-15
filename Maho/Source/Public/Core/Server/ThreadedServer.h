#pragma once

#include <Core/Misc/Export.h>
#include <Core/Server/ServerTask.h>
#include <Core/Server/TaskContext.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace Maho
{

/**
 * Client-server worker: one persistent thread + FIFO task queue + context arena.
 *
 * Context lifetime is owned by the server:
 *   AllocContext → (optional fill on game thread) → Enqueue(task with id)
 *   → Execute → server recycles the context automatically
 * Task only carries FTaskContextId; it never holds the context allocation.
 *
 * Example:
 * ```
 *   struct FLoadContext : Maho::FTaskContext { std::string Path; };
 *
 *   const Maho::FTaskContextId Id = Server.AllocContext<FLoadContext>();
 *   Server.GetContextAs<FLoadContext>(Id)->Path = "A.png";
 *   Server.Enqueue(std::make_unique<Maho::FLambdaServerTask>(Id,
 *       [Id](Maho::FThreadedServer& S)
 *       {
 *           auto* Ctx = S.GetContextAs<FLoadContext>(Id);
 *           // load Ctx->Path on the worker thread
 *       }));
 *   Server.Flush();
 * ```
 */
class MAHO_API FThreadedServer
{
public:
	FThreadedServer();
	virtual ~FThreadedServer();

	FThreadedServer(const FThreadedServer&) = delete;
	FThreadedServer& operator=(const FThreadedServer&) = delete;

	bool Initialize();
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const
	{
		return bInitialized;
	}
	[[nodiscard]] bool IsRunning() const;

	/**
	 * Allocate a context in server-owned storage. Takes ownership of InContext.
	 * Prefer AllocContext<T>(...) so construction + ownership stay under the server API.
	 * Bind the returned id to a task; it is freed when that task finishes Execute.
	 */
	FTaskContextId AllocContext(std::unique_ptr<FTaskContext> InContext);

	template<typename TContext, typename... TArgs>
	FTaskContextId AllocContext(TArgs&&... Args)
	{
		static_assert(std::is_base_of_v<FTaskContext, TContext>, "TContext must derive from FTaskContext");
		return AllocContext(std::unique_ptr<FTaskContext>(std::make_unique<TContext>(std::forward<TArgs>(Args)...)));
	}

	/** Look up a live context. Returns nullptr if id is stale or already recycled. */
	[[nodiscard]] FTaskContext* GetContext(FTaskContextId Id) const;

	template<typename TContext>
	[[nodiscard]] TContext* GetContextAs(FTaskContextId Id) const
	{
		return static_cast<TContext*>(GetContext(Id));
	}

	/** Non-blocking submit. Takes ownership of the task. */
	void Enqueue(std::unique_ptr<FServerTask> Task);

	/** Non-blocking submit of a lambda (wraps FLambdaServerTask). */
	void Enqueue(std::function<void(FThreadedServer&)> Function);

	/** Block until every task submitted before this call has finished Execute(). */
	void Flush();

protected:
	[[nodiscard]] virtual const char* GetServerThreadName() const = 0;
	[[nodiscard]] virtual const char* GetServerLogName() const = 0;

	virtual bool OnInitialize()
	{
		return true;
	}
	virtual void OnShutdown()
	{
	}

private:
	/** Recycle a server-owned context after its task completes. */
	void FreeContext(FTaskContextId Id);

	struct FImpl;
	std::unique_ptr<FImpl> Impl;
	bool bInitialized = false;

	const char* CachedThreadName = nullptr;
	const char* CachedLogName = nullptr;
};

} // namespace Maho
