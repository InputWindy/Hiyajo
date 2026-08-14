#include <RHI/RHIServer.h>

#include <Core/Misc/Log.h>
#include "VulkanRHI.h"
#include "../UI/ImGuiDrawDataRing.h"

#include <atomic>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

namespace Maho
{

FRHIServer::FRHIServer() = default;

FRHIServer::~FRHIServer()
{
	if (IsInitialized())
	{
		Shutdown();
	}
}

bool FRHIServer::OnInitialize()
{
	return true;
}

void FRHIServer::OnShutdown()
{
	ShutdownRHI();
	ResetFrameFence();
	MAHO_CORE_INFO("RHIServer: RHI worker shut down");
}

void FRHIServer::ResetFrameFence()
{
	std::lock_guard<std::mutex> Lock(FenceMutex);
	LastCompletedRenderFrame = 0;
}

void FRHIServer::WaitForRenderFrame(std::uint64_t FrameIndex)
{
	std::unique_lock<std::mutex> Lock(FenceMutex);
	FenceCv.wait(Lock, [this, FrameIndex]()
	{
		return LastCompletedRenderFrame >= FrameIndex;
	});
}

void FRHIServer::SignalRenderFrameComplete(std::uint64_t FrameIndex)
{
	{
		std::lock_guard<std::mutex> Lock(FenceMutex);
		if (FrameIndex > LastCompletedRenderFrame)
		{
			LastCompletedRenderFrame = FrameIndex;
		}
	}
	FenceCv.notify_all();
}

void FRHIServer::ShutdownRHI()
{
	if (!RHI)
	{
		return;
	}

	if (!IsInitialized())
	{
		RHI.reset();
		return;
	}

	Enqueue([this](FThreadedServer& /*Server*/)
	{
		if (RHI)
		{
			RHI->Shutdown();
			RHI.reset();
		}
	});
	Flush();
}

bool FRHIServer::InitializeRHI(FPlatformWindow& InWindow, ERHIBackend Backend)
{
	if (!IsInitialized())
	{
		MAHO_CORE_ERROR("RHIServer::InitializeRHI: server not initialized");
		return false;
	}

	if (!InWindow.HasOsWindow())
	{
		MAHO_CORE_INFO("RHIServer::InitializeRHI: headless; skipping RHI");
		return true;
	}

	void* NativeHandle = InWindow.GetNativeHandle();
	if (!NativeHandle)
	{
		MAHO_CORE_ERROR("RHIServer::InitializeRHI: native window handle is null");
		return false;
	}

	int Width = 0;
	int Height = 0;
	InWindow.GetFramebufferSize(Width, Height);
	if (Width <= 0 || Height <= 0)
	{
		MAHO_CORE_ERROR("RHIServer::InitializeRHI: invalid framebuffer {}x{}", Width, Height);
		return false;
	}

	std::atomic<bool> bOk{false};
	Enqueue([this, Backend, NativeHandle, Width, Height, &bOk](FThreadedServer& /*Server*/)
	{
		RHI = FRHIFactory::Create(Backend);
		if (!RHI)
		{
			bOk.store(false);
			return;
		}

		FRHIInitDesc Desc;
		Desc.Backend = Backend;
		Desc.NativeWindowHandle = NativeHandle;
		Desc.FramebufferWidth = Width;
		Desc.FramebufferHeight = Height;

		const bool bInitialized = RHI->Initialize(Desc);
		if (!bInitialized)
		{
			RHI.reset();
		}
		bOk.store(bInitialized);
	});
	Flush();

	if (!bOk.load())
	{
		MAHO_CORE_ERROR("RHIServer::InitializeRHI failed");
		return false;
	}

	MAHO_CORE_INFO("RHIServer RHI ready ({}x{})", Width, Height);
	return true;
}

FVulkanRHI* FRHIServer::GetVulkanRHI() const
{
	return dynamic_cast<FVulkanRHI*>(RHI.get());
}

void FRHIServer::SubmitBeginMainPass(float R, float G, float B, float A)
{
	if (!RHI)
	{
		return;
	}

	Enqueue([this, R, G, B, A](FThreadedServer& /*Server*/)
	{
		if (!RHI)
		{
			return;
		}

		FVulkanRHI* VulkanRHI = dynamic_cast<FVulkanRHI*>(RHI.get());
		if (!VulkanRHI)
		{
			RHI->BeginFrame();
			RHI->Clear(R, G, B, A);
			return;
		}

		VulkanRHI->BeginFrame();
		VulkanRHI->BeginMainPass(R, G, B, A);
	});
}

void FRHIServer::SubmitRenderUI(FImGuiDrawDataRing& Ring, int SlotIndex)
{
	if (!RHI || SlotIndex < 0)
	{
		return;
	}

	Enqueue([this, &Ring, SlotIndex](FThreadedServer& /*Server*/)
	{
		FVulkanRHI* VulkanRHI = dynamic_cast<FVulkanRHI*>(RHI.get());
		if (!VulkanRHI)
		{
			return;
		}

		FImGuiFrameSlot& Slot = Ring.Slots[static_cast<std::size_t>(SlotIndex)];
		if (!Slot.bOccupied || !Slot.DrawData.Valid || Slot.DrawData.CmdListsCount <= 0)
		{
			return;
		}

		ImGui_ImplVulkan_RenderDrawData(&Slot.DrawData, VulkanRHI->GetVkCommandBuffer());
	});
}

void FRHIServer::SubmitRenderPlatformWindows()
{
	if (!RHI)
	{
		return;
	}

	Enqueue([](FThreadedServer& /*Server*/)
	{
		ImGui::RenderPlatformWindowsDefault();
	});
}

void FRHIServer::SubmitEndFrameAndFence(std::uint64_t FrameIndex)
{
	if (!IsInitialized())
	{
		SignalRenderFrameComplete(FrameIndex);
		return;
	}

	Enqueue([this, FrameIndex](FThreadedServer& /*Server*/)
	{
		if (RHI)
		{
			FVulkanRHI* VulkanRHI = dynamic_cast<FVulkanRHI*>(RHI.get());
			if (!VulkanRHI)
			{
				RHI->EndFrame();
			}
			else
			{
				VulkanRHI->EndMainPass();
				VulkanRHI->EndFrame();
			}
		}

		// Do not IM_DELETE ImGui clones here — shared with ImDrawListSharedData; free on game thread.
		SignalRenderFrameComplete(FrameIndex);
	});
}

void FRHIServer::RequestResize(int Width, int Height)
{
	if (!RHI || Width <= 0 || Height <= 0)
	{
		return;
	}

	Enqueue([this, Width, Height](FThreadedServer& /*Server*/)
	{
		if (RHI)
		{
			RHI->Resize(Width, Height);
		}
	});
}

} // namespace Maho
