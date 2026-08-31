// RHI plugin - single codegen TU. The engine builds one Private/<Name>.cpp per
// plugin; the RHI's other translation units are folded in here (unity build).
//
// VMA_IMPLEMENTATION must be set BEFORE any include: VulkanMemory.h pulls
// vk_mem_alloc.h and is #pragma-once'd, so the second include (from
// VulkanMemory.cpp) would be skipped and the VMA bodies never compiled.
#define VMA_IMPLEMENTATION

#include <RHI/RHIServer.h>

#include <ConsoleVariable.h>
#include <Log.h>
#include <Platform.h>

#include <map>
#include <vector>

#include "RHI.h"
#include "VulkanCommandList.cpp"
#include "VulkanMemory.cpp"
#include "VulkanResources.cpp"
#include "VulkanRHI.cpp"

namespace Maho
{

namespace
{

static ConsoleVariable::TAutoConsoleVariable<int> GCVarRHIWidth(
	"r.RHI.Framebuffer.Width",
	1280,
	"RHI framebuffer width (used until a real window is bound)");

static ConsoleVariable::TAutoConsoleVariable<int> GCVarRHIHeight(
	"r.RHI.Framebuffer.Height",
	720,
	"RHI framebuffer height (used until a real window is bound)");

static ConsoleVariable::TAutoConsoleVariable<std::string> GCVarRHIBackend(
	"r.RHI.Backend",
	"vulkan",
	"RHI backend name (vulkan)");

[[nodiscard]] ERHIBackend BackendFromName(std::string_view Name)
{
	if (Name == "vulkan" || Name == "Vulkan")
	{
		return ERHIBackend::Vulkan;
	}
	MAHO_LOG_CORE_WARN("FRHI: unknown backend '{}' - falling back to Vulkan", std::string(Name));
	return ERHIBackend::Vulkan;
}

} // namespace

// -- lifecycle ---------------------------------------------------------------

FRHI::FRHI() = default;

FRHI::~FRHI() = default;

bool FRHI::Initialize(void* NativeWindowHandle, int Width, int Height, ERHIBackend Backend)
{
	if (!NativeWindowHandle)
	{
		MAHO_LOG_CORE_INFO("FRHI::Initialize: headless; skipping RHI");
		return true;
	}

	if (Width <= 0 || Height <= 0)
	{
		MAHO_LOG_CORE_ERROR("FRHI::Initialize: invalid framebuffer {}x{}", Width, Height);
		return false;
	}

	RHI.reset(FRHIFactory::Create(Backend));
	if (!RHI)
	{
		MAHO_LOG_CORE_ERROR("FRHI::Initialize: FRHIFactory::Create failed");
		return false;
	}

	FRHIInitDesc Desc;
	Desc.Backend = Backend;
	Desc.NativeWindowHandle = NativeWindowHandle;
	Desc.FramebufferWidth = Width;
	Desc.FramebufferHeight = Height;

	if (!RHI->Initialize(Desc))
	{
		MAHO_LOG_CORE_ERROR("FRHI::Initialize: backend initialize failed");
		RHI.reset();
		return false;
	}

	// Start the render server thread.
	FThreadedServer::Initialize();

	MAHO_LOG_CORE_INFO("FRHI RHI ready ({}x{})", Width, Height);
	return true;
}

void FRHI::ShutdownRHI()
{
	// Stop the render server thread first (idempotent).
	FThreadedServer::Shutdown();

	if (RHI)
	{
		RHI->Shutdown();
		RHI.reset();
	}
}

// -- frame pipeline (server-thread ordered) ----------------------------------

void FRHI::EnqueueTask(
	FRHICommandList* CmdList,
	std::function<void(FRHICommandList*)> Task)
{
	if (CmdList == nullptr || !Task)
	{
		return;
	}
	// Parallel command recording - each task owns its command list (Vulkan
	// forbids concurrent recording into the same buffer, so callers must never
	// share a CmdList across tasks). Record here; the caller (RDG) submits the
	// recorded lists serially afterwards via Submit.
	RecordingPool.Submit([CmdList, Task = std::move(Task)]()
	{
		CmdList->Begin();
		Task(CmdList);
		CmdList->End();
	});
}

void FRHI::Flush()
{
	// Drain all pending recording tasks - guarantees every EnqueueTask finished
	// before the caller proceeds to Submit (record-all -> submit-all ordering).
	RecordingPool.Flush();
}

// -- frame primitives - direct forwarding (caller guarantees queue serial) --

void FRHI::BeginFrame()
{
	if (RHI)
	{
		RHI->BeginFrame();
	}
}

void FRHI::Clear(float R, float G, float B, float A)
{
	if (RHI)
	{
		RHI->Clear(R, G, B, A);
	}
}

void FRHI::EndFrame()
{
	if (RHI)
	{
		RHI->EndFrame();
	}
}

void FRHI::Resize(int Width, int Height)
{
	if (RHI && Width > 0 && Height > 0)
	{
		RHI->Resize(Width, Height);
	}
}

void FRHI::DrawPrimitive(
	FRHIGraphicsPipeline* Pipeline,
	std::uint32_t VertexCount,
	std::uint32_t ViewportWidth,
	std::uint32_t ViewportHeight,
	float ClearR, float ClearG, float ClearB, float ClearA)
{
	if (RHI)
	{
		RHI->DrawPrimitive(Pipeline, VertexCount, ViewportWidth, ViewportHeight, ClearR, ClearG, ClearB, ClearA);
	}
}

// -- command lists -----------------------------------------------------------

FRHICommandList* FRHI::CreateCommandList(ERHICommandListType Type)
{
	return RHI ? RHI->CreateCommandList(Type) : nullptr;
}

void FRHI::DestroyCommandList(FRHICommandList* CmdList)
{
	if (RHI)
	{
		RHI->DestroyCommandList(CmdList);
	}
}

void FRHI::Submit(
	FRHICommandList* CmdList,
	ERHICommandListType Type,
	FRHISemaphore* const* WaitSemaphores,
	std::uint32_t WaitCount,
	FRHISemaphore* const* SignalSemaphores,
	std::uint32_t SignalCount,
	FRHIFence* SignalFence)
{
	if (!RHI || CmdList == nullptr)
	{
		return;
	}

	// Direct queue submit, routed to the queue matching the command-list type.
	// Cross-queue ordering: when the compute/transfer queue falls back to the
	// graphics family, submit there so it serializes with raster work. The
	// caller (RDG) must keep queue submissions serialized.
	switch (Type)
	{
	case ERHICommandListType::Compute:
		if (RHI->GetComputeQueue().IsNativeFallback())
		{
			RHI->GetGraphicsQueue().Submit(&CmdList, 1, WaitSemaphores, WaitCount, SignalSemaphores, SignalCount, SignalFence);
		}
		else
		{
			RHI->GetComputeQueue().Submit(&CmdList, 1, WaitSemaphores, WaitCount, SignalSemaphores, SignalCount, SignalFence);
		}
		break;
	case ERHICommandListType::Transfer:
		if (RHI->GetTransferQueue().IsNativeFallback())
		{
			RHI->GetGraphicsQueue().Submit(&CmdList, 1, WaitSemaphores, WaitCount, SignalSemaphores, SignalCount, SignalFence);
		}
		else
		{
			RHI->GetTransferQueue().Submit(&CmdList, 1, WaitSemaphores, WaitCount, SignalSemaphores, SignalCount, SignalFence);
		}
		break;
	default:
		RHI->GetGraphicsQueue().Submit(&CmdList, 1, WaitSemaphores, WaitCount, SignalSemaphores, SignalCount, SignalFence);
		break;
	}
}

bool FRHI::IsInitialized() const
{
	return RHI != nullptr && RHI->IsInitialized();
}

FRHIFence* FRHI::CreateFence(bool bSignaled)
{
	return RHI ? RHI->CreateFence(bSignaled) : nullptr;
}

void FRHI::DestroyFence(FRHIFence* Fence)
{
	if (RHI)
	{
		RHI->DestroyFence(Fence);
	}
}

FRHISemaphore* FRHI::CreateGpuSemaphore()
{
	return RHI ? RHI->CreateGpuSemaphore() : nullptr;
}

void FRHI::DestroyGpuSemaphore(FRHISemaphore* Semaphore)
{
	if (RHI)
	{
		RHI->DestroyGpuSemaphore(Semaphore);
	}
}

void FRHI::WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs)
{
	if (RHI)
	{
		RHI->WaitForFence(Fence, TimeoutNs);
	}
}

bool FRHI::IsFenceSignaled(FRHIFence* Fence)
{
	return RHI != nullptr && RHI->IsFenceSignaled(Fence);
}

// -- resource factories - direct forwarding ----------------------------------

FRHIBuffer* FRHI::CreateBuffer(const FRHIBufferDesc& Desc)
{
	return RHI ? RHI->CreateBuffer(Desc) : nullptr;
}

void FRHI::DestroyBuffer(FRHIBuffer* Buffer)
{
	if (RHI)
	{
		RHI->DestroyBuffer(Buffer);
	}
}

FRHITexture* FRHI::CreateTexture(const FRHITextureDesc& Desc)
{
	return RHI ? RHI->CreateTexture(Desc) : nullptr;
}

void FRHI::DestroyTexture(FRHITexture* Texture)
{
	if (RHI)
	{
		RHI->DestroyTexture(Texture);
	}
}

FRHISampler* FRHI::CreateSampler(const FRHISamplerDesc& Desc)
{
	return RHI ? RHI->CreateSampler(Desc) : nullptr;
}

void FRHI::DestroySampler(FRHISampler* Sampler)
{
	if (RHI)
	{
		RHI->DestroySampler(Sampler);
	}
}

FRHIShaderModule* FRHI::CreateShaderModule(const FRHIShaderModuleDesc& Desc)
{
	return RHI ? RHI->CreateShaderModule(Desc) : nullptr;
}

void FRHI::DestroyShaderModule(FRHIShaderModule* Module)
{
	if (RHI)
	{
		RHI->DestroyShaderModule(Module);
	}
}

FRHIGraphicsPipeline* FRHI::CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc)
{
	return RHI ? RHI->CreateGraphicsPipeline(Desc) : nullptr;
}

void FRHI::DestroyGraphicsPipeline(FRHIGraphicsPipeline* Pipeline)
{
	if (RHI)
	{
		RHI->DestroyGraphicsPipeline(Pipeline);
	}
}

FRHIComputePipeline* FRHI::CreateComputePipeline(const FRHIComputePipelineDesc& Desc)
{
	return RHI ? RHI->CreateComputePipeline(Desc) : nullptr;
}

void FRHI::DestroyComputePipeline(FRHIComputePipeline* Pipeline)
{
	if (RHI)
	{
		RHI->DestroyComputePipeline(Pipeline);
	}
}

FRHIStructuredBuffer* FRHI::CreateStructuredBuffer(const FRHIStructuredBufferDesc& Desc)
{
	return RHI ? RHI->CreateStructuredBuffer(Desc) : nullptr;
}

void FRHI::DestroyStructuredBuffer(FRHIStructuredBuffer* Buffer)
{
	if (RHI)
	{
		RHI->DestroyStructuredBuffer(Buffer);
	}
}

FRHIBufferView* FRHI::CreateBufferView(const FRHIBufferViewDesc& Desc)
{
	return RHI ? RHI->CreateBufferView(Desc) : nullptr;
}

void FRHI::DestroyBufferView(FRHIBufferView* View)
{
	if (RHI)
	{
		RHI->DestroyBufferView(View);
	}
}

FRHITextureView* FRHI::CreateTextureView(const FRHITextureViewDesc& Desc)
{
	return RHI ? RHI->CreateTextureView(Desc) : nullptr;
}

void FRHI::DestroyTextureView(FRHITextureView* View)
{
	if (RHI)
	{
		RHI->DestroyTextureView(View);
	}
}

FRHIDescriptorSetLayout* FRHI::CreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc& Desc)
{
	return RHI ? RHI->CreateDescriptorSetLayout(Desc) : nullptr;
}

void FRHI::DestroyDescriptorSetLayout(FRHIDescriptorSetLayout* Layout)
{
	if (RHI)
	{
		RHI->DestroyDescriptorSetLayout(Layout);
	}
}

FRHIPipelineLayout* FRHI::CreatePipelineLayout(const FRHIPipelineLayoutDesc& Desc)
{
	return RHI ? RHI->CreatePipelineLayout(Desc) : nullptr;
}

void FRHI::DestroyPipelineLayout(FRHIPipelineLayout* Layout)
{
	if (RHI)
	{
		RHI->DestroyPipelineLayout(Layout);
	}
}

FRHIDescriptorPool* FRHI::CreateDescriptorPool(const FRHIDescriptorPoolDesc& Desc)
{
	return RHI ? RHI->CreateDescriptorPool(Desc) : nullptr;
}

void FRHI::DestroyDescriptorPool(FRHIDescriptorPool* Pool)
{
	if (RHI)
	{
		RHI->DestroyDescriptorPool(Pool);
	}
}

FRHIDescriptorSet* FRHI::AllocateDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSetLayout* Layout)
{
	return RHI ? RHI->AllocateDescriptorSet(Pool, Layout) : nullptr;
}

void FRHI::FreeDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSet* Set)
{
	if (RHI)
	{
		RHI->FreeDescriptorSet(Pool, Set);
	}
}

FRHIRenderPass* FRHI::CreateRenderPass(const FRHIRenderPassDesc& Desc)
{
	return RHI ? RHI->CreateRenderPass(Desc) : nullptr;
}

void FRHI::DestroyRenderPass(FRHIRenderPass* Pass)
{
	if (RHI)
	{
		RHI->DestroyRenderPass(Pass);
	}
}

FRHIFramebuffer* FRHI::CreateFramebuffer(const FRHIFramebufferDesc& Desc)
{
	return RHI ? RHI->CreateFramebuffer(Desc) : nullptr;
}

void FRHI::DestroyFramebuffer(FRHIFramebuffer* Framebuffer)
{
	if (RHI)
	{
		RHI->DestroyFramebuffer(Framebuffer);
	}
}

FRHIFramebuffer* FRHI::GetBackBufferFramebuffer(std::uint32_t ImageIndex)
{
	return RHI ? RHI->GetBackBufferFramebuffer(ImageIndex) : nullptr;
}

FRHIRenderPass* FRHI::GetSwapchainRenderPass()
{
	return RHI ? RHI->GetSwapchainRenderPass() : nullptr;
}

std::uint32_t FRHI::GetCurrentBackBufferIndex() const
{
	return RHI ? RHI->GetCurrentBackBufferIndex() : 0;
}

std::uint32_t FRHI::GetFramebufferWidth() const
{
	return RHI ? RHI->GetFramebufferWidth() : 0;
}

std::uint32_t FRHI::GetFramebufferHeight() const
{
	return RHI ? RHI->GetFramebufferHeight() : 0;
}

FRHIQueryPool* FRHI::CreateQueryPool(ERHIQueryType Type, std::uint32_t QueryCount)
{
	return RHI ? RHI->CreateQueryPool(Type, QueryCount) : nullptr;
}

void FRHI::DestroyQueryPool(FRHIQueryPool* Pool)
{
	if (RHI)
	{
		RHI->DestroyQueryPool(Pool);
	}
}

bool FRHI::GetQueryPoolResults(
	FRHIQueryPool* Pool,
	std::uint32_t FirstQuery,
	std::uint32_t QueryCount,
	std::uint64_t* Results,
	std::size_t Stride,
	bool bWait)
{
	return RHI != nullptr && RHI->GetQueryPoolResults(Pool, FirstQuery, QueryCount, Results, Stride, bWait);
}

FRHIRayTracingPipeline* FRHI::CreateRayTracingPipeline(const FRHIRayTracingPipelineDesc& Desc)
{
	return RHI ? RHI->CreateRayTracingPipeline(Desc) : nullptr;
}

void FRHI::DestroyRayTracingPipeline(FRHIRayTracingPipeline* Pipeline)
{
	if (RHI)
	{
		RHI->DestroyRayTracingPipeline(Pipeline);
	}
}

FRHIAccelerationStructure* FRHI::CreateAccelerationStructure(const FRHIRayTracingGeometryDesc& Desc)
{
	return RHI ? RHI->CreateAccelerationStructure(Desc) : nullptr;
}

void FRHI::DestroyAccelerationStructure(FRHIAccelerationStructure* Accel)
{
	if (RHI)
	{
		RHI->DestroyAccelerationStructure(Accel);
	}
}

bool FRHI::GetAccelerationStructureBuildSizes(
	const FRHIRayTracingGeometryDesc& Desc,
	std::uint64_t& OutAccelSize,
	std::uint64_t& OutScratchSize)
{
	return RHI != nullptr && RHI->GetAccelerationStructureBuildSizes(Desc, OutAccelSize, OutScratchSize);
}

FRHIBuffer* FRHI::CreateShaderBindingTable(
	FRHIRayTracingPipeline* Pipeline,
	const FRHISbtGroup* Groups,
	std::uint32_t GroupCount,
	std::uint32_t* OutRayGenOffset,
	std::uint32_t* OutRayGenStride,
	std::uint32_t* OutHitOffset,
	std::uint32_t* OutHitStride,
	std::uint32_t* OutMissOffset,
	std::uint32_t* OutMissStride)
{
	return RHI
		? RHI->CreateShaderBindingTable(Pipeline, Groups, GroupCount,
			OutRayGenOffset, OutRayGenStride, OutHitOffset, OutHitStride,
			OutMissOffset, OutMissStride)
		: nullptr;
}

} // namespace Maho
