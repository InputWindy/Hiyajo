#pragma once

#include "RenderApi.h"
#include <Core/Server/ThreadedServer.h>
#include <PlatformWindow.h>
#include <RHI/RHI.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace Maho
{

class FVulkanRHI;
struct FImGuiDrawDataRing;

/**
 * RHI worker thread (MahoRHI). Owns FRHI and executes Submit* on the server queue.
 * Short fixed pipeline — not an extensible Extension/DependsPack system.
 * Owned by FRenderSystem.
 */
class MAHO_RENDER_API FRHIServer final : public FThreadedServer
{
public:
	FRHIServer();
	~FRHIServer() override;

	FRHIServer(const FRHIServer&) = delete;
	FRHIServer& operator=(const FRHIServer&) = delete;

	[[nodiscard]] bool HasRHI() const
	{
		return static_cast<bool>(RHI);
	}
	[[nodiscard]] FVulkanRHI* GetVulkanRHI() const;
	[[nodiscard]] IRHI* GetRHI() const
	{
		return RHI.get();
	}

	void WaitForRenderFrame(std::uint64_t FrameIndex);
	void SignalRenderFrameComplete(std::uint64_t FrameIndex);
	void ResetFrameFence();

	[[nodiscard]] bool InitializeRHI(FPlatformWindow& InWindow, ERHIBackend Backend = ERHIBackend::Vulkan);
	void ShutdownRHI();

	void SubmitBeginMainPass(float R, float G, float B, float A);
	/** SlotIndex from FImGuiDrawDataRing::CaptureFromImGui; ring owned by FRenderSystem. */
	void SubmitRenderUI(FImGuiDrawDataRing& Ring, int SlotIndex);
	/** Secondary ImGui viewports: ImGui::RenderPlatformWindowsDefault on MahoRHI. */
	void SubmitRenderPlatformWindows();
	void SubmitEndFrameAndFence(std::uint64_t FrameIndex);
	void RequestResize(int Width, int Height);

protected:
	[[nodiscard]] const char* GetServerThreadName() const override
	{
		return "MahoRHI";
	}
	[[nodiscard]] const char* GetServerLogName() const override
	{
		return "RHIServer";
	}

	bool OnInitialize() override;
	void OnShutdown() override;

private:
	FRHIPtr RHI;

	std::mutex FenceMutex;
	std::condition_variable FenceCv;
	std::uint64_t LastCompletedRenderFrame = 0;
};

} // namespace Maho
