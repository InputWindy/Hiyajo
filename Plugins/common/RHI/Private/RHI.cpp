// RHI plugin - single codegen TU. The engine builds one Private/<Name>.cpp per
// plugin; the RHI's other translation units are folded in here (unity build).
//
// VMA_IMPLEMENTATION must be set BEFORE any include: VulkanMemory.h pulls
// vk_mem_alloc.h and is #pragma-once'd, so the second include (from
// VulkanMemory.cpp) would be skipped and the VMA bodies never compiled.
#define VMA_IMPLEMENTATION

#include <RHI/RHIServer.h>

#include <Log.h>

#include <atomic>

#include "RHIResourceManager.cpp"
#include "VulkanCommandList.cpp"
#include "VulkanMemory.cpp"
#include "VulkanResources.cpp"
#include "VulkanRHI.cpp"

namespace Maho
{

FRHISystem& FRHISystem::Get()
{
	static FRHISystem Instance;
	return Instance;
}

FRHISystem::FRHISystem() = default;

FRHISystem::~FRHISystem()
{
	if (IsRunning())
	{
		Shutdown();
	}
}

void FRHISystem::Initialize(int Argc, char** Argv)
{
	(void)Argc; (void)Argv;
	FThreadedServer::Initialize();
}

void FRHISystem::Shutdown()
{
	FThreadedServer::Shutdown();
}

bool FRHISystem::OnInitialize()
{
	return true;
}

void FRHISystem::OnShutdown()
{
	ShutdownRHI();
	ResetFrameFence();
	MAHO_LOG_CORE_INFO("RHISystem: RHI worker shut down");
}

void FRHISystem::ResetFrameFence()
{
	std::lock_guard<std::mutex> Lock(FenceMutex);
	LastCompletedRenderFrame = 0;
}

void FRHISystem::WaitForRenderFrame(std::uint64_t FrameIndex)
{
	std::unique_lock<std::mutex> Lock(FenceMutex);
	FenceCv.wait(Lock, [this, FrameIndex]()
	{
		return LastCompletedRenderFrame >= FrameIndex;
	});
}

void FRHISystem::SignalRenderFrameComplete(std::uint64_t FrameIndex)
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

void FRHISystem::ShutdownRHI()
{
	if (!RHI)
	{
		return;
	}

	if (!IsRunning())
	{
		RHI.reset();
		return;
	}

	Submit([this]
	{
		if (RHI)
		{
			RHI->Shutdown();
			RHI.reset();
		}
	});
	Flush();
}

bool FRHISystem::InitializeRHI(void* NativeWindowHandle, int Width, int Height, ERHIBackend Backend)
{
	if (!IsRunning())
	{
		MAHO_LOG_CORE_ERROR("RHISystem::InitializeRHI: server not initialized");
		return false;
	}

	if (!NativeWindowHandle)
	{
		MAHO_LOG_CORE_INFO("RHISystem::InitializeRHI: headless; skipping RHI");
		return true;
	}

	if (Width <= 0 || Height <= 0)
	{
		MAHO_LOG_CORE_ERROR("RHISystem::InitializeRHI: invalid framebuffer {}x{}", Width, Height);
		return false;
	}

	std::atomic<bool> bOk{false};
	Submit([this, Backend, NativeWindowHandle, Width, Height, &bOk]
	{
		RHI = FRHIFactory::Create(Backend);
		if (!RHI)
		{
			bOk.store(false);
			return;
		}

		FRHIInitDesc Desc;
		Desc.Backend = Backend;
		Desc.NativeWindowHandle = NativeWindowHandle;
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
		MAHO_LOG_CORE_ERROR("RHISystem::InitializeRHI failed");
		return false;
	}

	MAHO_LOG_CORE_INFO("RHISystem RHI ready ({}x{})", Width, Height);
	return true;
}

void FRHISystem::SubmitBeginFrame(float R, float G, float B, float A)
{
	if (!RHI)
	{
		return;
	}

	Submit([this, R, G, B, A]
	{
		if (!RHI)
		{
			return;
		}
		RHI->BeginFrame();
		RHI->Clear(R, G, B, A);
	});
}

void FRHISystem::SubmitEndFrame(std::uint64_t FrameIndex)
{
	if (!IsRunning())
	{
		SignalRenderFrameComplete(FrameIndex);
		return;
	}

	Submit([this, FrameIndex]
	{
		if (RHI)
		{
			RHI->EndFrame();
		}
		SignalRenderFrameComplete(FrameIndex);
	});
}

void FRHISystem::RequestResize(int Width, int Height)
{
	if (!RHI || Width <= 0 || Height <= 0)
	{
		return;
	}

	Submit([this, Width, Height]
	{
		if (RHI)
		{
			RHI->Resize(Width, Height);
		}
	});
}

} // namespace Maho
