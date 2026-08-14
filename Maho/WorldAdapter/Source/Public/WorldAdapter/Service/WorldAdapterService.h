#pragma once

#include <WorldAdapter/Core/CommandQueue.h>
#include <WorldAdapter/Transport/HttpTransport.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Maho
{

struct FWorldAdapterServiceConfig
{
	std::uint16_t Port = 8770;
	std::size_t QueueCapacity = WorldAdapterDefaultQueueCapacity;
	std::chrono::milliseconds RequestTimeout = std::chrono::milliseconds(5000);
	std::size_t RequestBodyLimit = WorldAdapterRequestBodyLimit;
	std::size_t ResponseBodyLimit = WorldAdapterResponseBodyLimit;
	std::size_t HttpWorkerCount = 4;
	std::size_t HttpConnectionQueueCapacity = 64;
	std::string BearerToken;
};

class FWorldAdapterService
{
public:
	explicit FWorldAdapterService(std::unique_ptr<IWorldAdapterBackend> Backend);
	~FWorldAdapterService();

	FWorldAdapterService(const FWorldAdapterService&) = delete;
	FWorldAdapterService& operator=(const FWorldAdapterService&) = delete;

	[[nodiscard]] bool Initialize(const FWorldAdapterServiceConfig& Config, std::string& OutError) noexcept;
	[[nodiscard]] std::size_t Pump(std::size_t MaximumCommands = 16) noexcept;

	void BeginShutdown() noexcept;
	void Shutdown() noexcept;

	[[nodiscard]] bool IsRunning() const noexcept;
	[[nodiscard]] bool IsShuttingDown() const noexcept;
	[[nodiscard]] std::uint16_t GetPort() const noexcept;
	[[nodiscard]] const char* GetHost() const noexcept { return "127.0.0.1"; }

private:
	[[nodiscard]] FWorldAdapterHttpResponse HandleHttpRequest(const FWorldAdapterHttpRequest& Request) noexcept;

	std::unique_ptr<IWorldAdapterBackend> Backend;
	std::unique_ptr<FWorldAdapterCommandQueue> Queue;
	std::unique_ptr<FWorldAdapterHttpTransport> Transport;
	FWorldAdapterServiceConfig Config;
	FWorldAdapterCapabilities Capabilities;
	std::atomic<bool> bInitialized = false;
	std::atomic<bool> bShuttingDown = false;
};

} // namespace Maho
