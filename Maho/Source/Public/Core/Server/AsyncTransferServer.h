#pragma once

/**
 * Templated async transfer server — inherits FThreadedServer for async thread
 * capability, exposes structured Import/Export pipeline.
 *
 * TRequest  — submitted by client; moved into the server on Submit.
 * TResult   — produced by ExecuteRequest; retrieved by client via RetrieveResult.
 *
 * Usage:
 *   class FResourceServer : public TAsyncTransferServer<FLoadRequest, FResourceBulkData>
 *   {
 *   protected:
 *       FResourceBulkData ExecuteRequest(const FLoadRequest& Req) override { ... }
 *   };
 *
 *   // Client:
 *   auto Handle = Server->Submit(Req);
 *   if (Handle.HasSucceeded())
 *       auto Result = Server->RetrieveResult(Handle);
 */

#include <Core/Misc/Export.h>
#include <Core/Server/ThreadedServer.h>
#include <Core/Server/TransferHandle.h>

#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Maho
{

template <typename TRequest, typename TResult>
class TAsyncTransferServer : public FThreadedServer
{
public:
	TAsyncTransferServer() = default;
	virtual ~TAsyncTransferServer()
	{
		Shutdown();
	}

	TAsyncTransferServer(const TAsyncTransferServer&) = delete;
	TAsyncTransferServer& operator=(const TAsyncTransferServer&) = delete;

	[[nodiscard]] FTransferHandle Submit(TRequest Request)
	{
		if (!IsInitialized())
			return {};

		FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
		if (!Handle.IsValid())
			return {};

		{
			std::lock_guard<std::mutex> Lock(ResultMutex);
			PendingResults.emplace(Handle.Id, FPendingEntry{});
		}

		Enqueue([this, Handle, Req = std::move(Request)](FThreadedServer& /*Server*/) mutable
		{
			TResult Result = ExecuteRequest(Req);

			std::lock_guard<std::mutex> Lock(ResultMutex);
			auto It = PendingResults.find(Handle.Id);
			if (It != PendingResults.end())
			{
				It->second.Result = std::move(Result);
				It->second.bReady = true;
				SetTransferHandleState(Handle, ETransferState::Succeeded);
			}
			else
			{
				SetTransferHandleState(Handle, ETransferState::Failed);
			}
			ResultCv.notify_all();
		});

		return Handle;
	}

	[[nodiscard]] TResult RetrieveResult(FTransferHandle Handle)
	{
		if (!Handle.IsValid() || !Handle.HasSucceeded())
			return TResult{};

		std::lock_guard<std::mutex> Lock(ResultMutex);
		auto It = PendingResults.find(Handle.Id);
		if (It == PendingResults.end() || !It->second.bReady)
			return TResult{};

		TResult Result = std::move(It->second.Result);
		PendingResults.erase(It);
		return Result;
	}

	void Flush(FTransferHandle Handle)
	{
		if (!Handle.IsValid())
			return;

		std::unique_lock<std::mutex> Lock(ResultMutex);
		ResultCv.wait(Lock, [this, Handle]()
		{
			auto It = PendingResults.find(Handle.Id);
			return It == PendingResults.end() || It->second.bReady;
		});
	}

	void Release(FTransferHandle Handle)
	{
		if (!Handle.IsValid())
			return;

		{
			std::lock_guard<std::mutex> Lock(ResultMutex);
			PendingResults.erase(Handle.Id);
		}
		ResultCv.notify_all();
	}

protected:
	virtual TResult ExecuteRequest(const TRequest& Request) = 0;

	virtual void OnShutdown() override
	{
		FThreadedServer::Flush();
		std::lock_guard<std::mutex> Lock(ResultMutex);
		PendingResults.clear();
	}

private:
	struct FPendingEntry
	{
		TResult Result{};
		bool bReady = false;
	};

	std::mutex ResultMutex;
	std::condition_variable ResultCv;
	std::unordered_map<std::uint64_t, FPendingEntry> PendingResults;
};

} // namespace Maho