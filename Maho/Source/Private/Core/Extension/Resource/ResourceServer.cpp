#include <Core/Extension/Resource/ResourceServer.h>

#include <Core/Misc/Log.h>
#include <Core/Misc/Utf8Path.h>

#include <filesystem>
#include <fstream>

namespace Maho
{

FResourceServer::~FResourceServer()
{
	Shutdown();
}

void FResourceServer::OnShutdown()
{
	TAsyncTransferServer::OnShutdown();
	MAHO_CORE_INFO("ResourceServer shut down");
}

FResourceLoadResult FResourceServer::ExecuteRequest(const FResourceLoadRequest& Request)
{
	FResourceLoadResult Result;
	Result.SourcePath = Request.Path;

	bool bSuccess = false;
	std::vector<std::uint8_t> Bytes;

	namespace fs = std::filesystem;
	std::error_code ErrorCode;
	const fs::path FilePath = PathFromUtf8(Request.Path);

	if (!fs::is_regular_file(FilePath, ErrorCode) || ErrorCode)
	{
		MAHO_CORE_ERROR("Resource BulkData failed: path=\"{}\" (not a regular file)", Request.Path);
	}
	else
	{
		std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
		if (!File)
		{
			MAHO_CORE_ERROR("Resource BulkData failed: path=\"{}\" (open failed)", Request.Path);
		}
		else
		{
			const std::streamoff Size = File.tellg();
			if (Size < 0)
			{
				MAHO_CORE_ERROR("Resource BulkData failed: path=\"{}\" (size failed)", Request.Path);
			}
			else
			{
				File.seekg(0, std::ios::beg);
				Bytes.resize(static_cast<std::size_t>(Size));
				if (Size > 0 && !File.read(reinterpret_cast<char*>(Bytes.data()), Size))
				{
					Bytes.clear();
					MAHO_CORE_ERROR("Resource BulkData failed: path=\"{}\" (read failed)", Request.Path);
				}
				else
				{
					bSuccess = true;
					MAHO_CORE_INFO("Resource BulkData ready: path=\"{}\" bytes={}", Request.Path, Bytes.size());
				}
			}
		}
	}

	if (!bSuccess)
		Bytes.clear();
	Result.Bytes = std::move(Bytes);
	return Result;
}

bool FResourceServer::TryTakeBulkData(FTransferHandle Handle, FResourceBulkData& OutBulk)
{
	if (!Handle.IsValid() || !Handle.HasSucceeded())
		return false;

	FResourceLoadResult Result = RetrieveResult(Handle);
	if (Result.Bytes.empty())
		return false;

	OutBulk.SourcePath = std::move(Result.SourcePath);
	OutBulk.Bytes = std::move(Result.Bytes);
	return true;
}

bool FResourceServer::HasPendingLoads() const
{
	return false;
}

} // namespace Maho
