#pragma once

#include <Core/Misc/Export.h>
#include <Core/Server/TaskContext.h>

#include <functional>

namespace Maho
{

class FThreadedServer;

/**
 * Base work item for FThreadedServer.
 * Carries an FTaskContextId into server-owned storage.
 * When Execute returns, the server always recycles that context (if the id is valid).
 *
 * Example:
 * ```
 *   class FUploadTask : public Maho::FServerTask
 *   {
 *   public:
 *       explicit FUploadTask(Maho::FTaskContextId Id) : Maho::FServerTask(Id) {}
 *       virtual void Execute(Maho::FThreadedServer& Server) override
 *       {
 *           // run on the server thread; context freed when this returns
 *       }
 *   };
 *   Server.Enqueue(std::make_unique<FUploadTask>(ContextId));
 * ```
 */
class MAHO_API FServerTask
{
public:
	FServerTask() = default;

	explicit FServerTask(FTaskContextId InContextId)
		: ContextId(InContextId)
	{
	}

	virtual ~FServerTask() = default;

	FServerTask(const FServerTask&) = delete;
	FServerTask& operator=(const FServerTask&) = delete;

	virtual void Execute(FThreadedServer& Server) = 0;

	[[nodiscard]] FTaskContextId GetContextId() const
	{
		return ContextId;
	}

protected:
	FTaskContextId ContextId{};
};

/** Convenience task that runs a callable on the server thread. */
class MAHO_API FLambdaServerTask final : public FServerTask
{
public:
	explicit FLambdaServerTask(std::function<void(FThreadedServer&)> InFunction)
		: Function(std::move(InFunction))
	{
	}

	FLambdaServerTask(FTaskContextId InContextId, std::function<void(FThreadedServer&)> InFunction)
		: FServerTask(InContextId)
		, Function(std::move(InFunction))
	{
	}

	virtual void Execute(FThreadedServer& Server) override
	{
		if (Function)
		{
			Function(Server);
		}
	}

private:
	std::function<void(FThreadedServer&)> Function;
};

} // namespace Maho
