#pragma once

/**
 * Private async BulkData loader for FResourceSystem.
 * Uses TAsyncTransferServer for structured Import/Export pipeline.
 * SoftPath / CatalogKey stay on FResourceSystem.
 */

#include <Core/Extension/Resource/ResourceSystem.h>
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
	EResourceBulkPreparedKind PreparedKind = EResourceBulkPreparedKind::None;
	std::shared_ptr<void> Prepared;
};

class MAHO_API FResourceServer : public TAsyncTransferServer<FResourceLoadRequest, FResourceLoadResult>
{
public:
	FResourceServer() = default;
	~FResourceServer() override;

	FResourceServer(const FResourceServer&) = delete;
	FResourceServer& operator=(const FResourceServer&) = delete;

	/** Begin async file load. Uses Submit() from TAsyncTransferServer. */
	[[nodiscard]] FTransferHandle RequestLoad(std::string Path)
	{
		FResourceLoadRequest Req;
		Req.Path = std::move(Path);
		return Submit(std::move(Req));
	}

	/** When Succeeded, move result out (one-shot). */
	[[nodiscard]] bool TryTakeBulkData(FTransferHandle Handle, FResourceBulkData& OutBulk);

	[[nodiscard]] bool HasPendingLoads() const;

protected:
	[[nodiscard]] const char* GetServerThreadName() const override { return "MahoResourceThread"; }
	[[nodiscard]] const char* GetServerLogName() const override { return "ResourceServer"; }

	bool OnInitialize() override { return true; }
	void OnShutdown() override;

	FResourceLoadResult ExecuteRequest(const FResourceLoadRequest& Request) override;
};

} // namespace Maho