#pragma once

#include <WorldAdapter/Protocol/Dto.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Maho
{

struct FWorldAdapterHttpRequest
{
	std::string Method;
	std::string Path;
	std::unordered_map<std::string, std::string> Headers;
	std::string Body;
};

struct FWorldAdapterHttpResponse
{
	int Status = 200;
	std::string Body;
};

struct FWorldAdapterHttpTransportConfig
{
	std::uint16_t Port = 8770;
	std::size_t RequestBodyLimit = WorldAdapterRequestBodyLimit;
	std::size_t ResponseBodyLimit = WorldAdapterResponseBodyLimit;
	std::size_t WorkerCount = 4;
	std::size_t ConnectionQueueCapacity = 64;
};

class FWorldAdapterHttpTransport
{
public:
	using FRequestHandler = std::function<FWorldAdapterHttpResponse(const FWorldAdapterHttpRequest&)>;

	FWorldAdapterHttpTransport();
	~FWorldAdapterHttpTransport();

	FWorldAdapterHttpTransport(const FWorldAdapterHttpTransport&) = delete;
	FWorldAdapterHttpTransport& operator=(const FWorldAdapterHttpTransport&) = delete;

	[[nodiscard]] bool Initialize(
		const FWorldAdapterHttpTransportConfig& Config,
		FRequestHandler Handler,
		std::string& OutError) noexcept;

	void BeginShutdown() noexcept;
	void Shutdown() noexcept;

	[[nodiscard]] bool IsRunning() const noexcept;
	[[nodiscard]] std::uint16_t GetPort() const noexcept;
	[[nodiscard]] const char* GetHost() const noexcept { return "127.0.0.1"; }

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

} // namespace Maho
