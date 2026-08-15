#pragma once

/**
 * Private async BulkData loader for FResourceSystem.
 * Uses TAsyncTransferServer for structured Import/Export pipeline.
 * SoftPath / CatalogKey stay on FResourceSystem.
 */

#include "ResourceApi.h"
#include <ResourceSystem.h>
#include <Core/Server/AsyncTransferServer.h>

#include <memory>
#include <string>
#include <vector>

namespace Maho
{

struct FResourceLoadRequest
{
	std::string Path;
};

struct FResourceLoadResult
{
	std::string SourcePath;
	std::vector<std::uint8_t> Bytes;
};

/**
 * Async file loader backed by TAsyncTransferServer.
 * FResourceSystem owns one server instance and drives request/result flow.
 */
class MAHO_RESOURCE_API FResourceServer : public TAsyncTransferServer<FResourceLoadRequest, FResourceLoadResult>
{
public:
	FResourceServer() = default;
	~FResourceServer() override;

	FResourceServer(const FResourceServer&) = delete;
	FResourceServer& operator=(const FResourceServer&) = delete;

	/** Begin async file load. Uses Submit() from TAsyncTransferServer. */
	[[nodiscard]] FTransferHandle RequestLoad(std::string Path);

	/** When Succeeded, move result out (one-shot). */
	[[nodiscard]] bool TryTakeBulkData(FTransferHandle Handle, FResourceBulkData& OutBulk);

	[[nodiscard]] bool HasPendingLoads() const;

protected:
	[[nodiscard]] const char* GetServerThreadName() const override;
	[[nodiscard]] const char* GetServerLogName() const override;

	bool OnInitialize() override;
	void OnShutdown() override;

	FResourceLoadResult ExecuteRequest(const FResourceLoadRequest& Request) override;
};

} // namespace Maho