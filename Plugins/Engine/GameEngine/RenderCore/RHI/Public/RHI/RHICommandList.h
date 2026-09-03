#pragma once

#include "RHIAPI.h"
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>

#include <cstdint>

namespace Maho
{

class FRHICommandList;

struct MAHO_RHI_API FRHIRenderingAttachmentInfo
{
	FRHITextureView* View = nullptr;
	ERHILoadOp LoadOp = ERHILoadOp::Clear;
	ERHIStoreOp StoreOp = ERHIStoreOp::Store;
	float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
};

/**
 * Logical submit endpoint (Graphics / Compute / Transfer).
 * Always exists for all three types; Transfer may map to another native queue under the hood.
 */
class MAHO_RHI_API FRHIQueue
{
public:
	virtual ~FRHIQueue() = default;

	[[nodiscard]] virtual ERHIQueueType GetType() const = 0;

	/** True when logical Transfer/Compute shares a non-dedicated native queue. Debug/logging only. */
	[[nodiscard]] virtual bool IsNativeFallback() const
	{
		return false;
	}

	virtual void Submit(
		FRHICommandList* const* CmdLists,
		std::uint32_t Count,
		FRHISemaphore* const* WaitSemaphores,
		std::uint32_t WaitCount,
		FRHISemaphore* const* SignalSemaphores,
		std::uint32_t SignalCount,
		FRHIFence* SignalFence) = 0;
};

/**
 * Command recording surface (~= VkCommandBuffer).
 * Capability depends on GetType(); illegal calls assert in Debug.
 */
class MAHO_RHI_API FRHICommandList
{
public:
	virtual ~FRHICommandList() = default;

	[[nodiscard]] virtual ERHICommandListType GetType() const = 0;

	virtual void Begin() = 0;
	virtual void End() = 0;

	// Transfer / Barrier (Transfer primary; Graphics/Compute allowed)
	virtual void CopyBuffer(
		FRHIBuffer* Src,
		std::uint64_t SrcOffset,
		FRHIBuffer* Dst,
		std::uint64_t DstOffset,
		std::uint64_t Size) = 0;
	virtual void CopyBufferToTexture(FRHIBuffer* Src, FRHITexture* Dst, std::uint64_t SrcOffset) = 0;
	virtual void CopyTextureToBuffer(FRHITexture* Src, FRHIBuffer* Dst, std::uint64_t DstOffset) = 0;
	virtual void FillBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, std::uint64_t Size, std::uint32_t Data) = 0;
	/**
	 * Upload CPU data into a buffer (recorded - runs inside EnqueueTask).
	 * Host-visible buffers are written directly; device-local buffers go
	 * through a staging copy.
	 */
	virtual void UpdateBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, std::uint64_t Size, const void* Data) = 0;
	/** Update descriptor sets (recorded - vkUpdateDescriptorSets, immediate CPU op). */
	virtual void UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, std::uint32_t Count) = 0;
	virtual void TransitionBuffer(FRHIBuffer* Buffer, ERHIResourceState OldState, ERHIResourceState NewState) = 0;
	virtual void TransitionTexture(FRHITexture* Texture, ERHIResourceState OldState, ERHIResourceState NewState) = 0;

	// Graphics only
	virtual void BeginRenderPass(
		FRHIRenderPass* RenderPass,
		FRHIFramebuffer* Framebuffer,
		std::uint32_t Width,
		std::uint32_t Height,
		const float ClearColor[4],
		bool bHasDepthStencil = false,
		float DepthClear = 1.0f,
		std::uint32_t StencilClear = 0) = 0;
	virtual void EndRenderPass() = 0;

	virtual void BeginRendering(
		const FRHIRenderingAttachmentInfo* ColorAttachments, std::uint32_t ColorCount,
		const FRHIRenderingAttachmentInfo* DepthAttachment,
		std::uint32_t Width, std::uint32_t Height) = 0;
	virtual void EndRendering() = 0;
	virtual void SetViewport(float X, float Y, float Width, float Height, float MinDepth = 0.0f, float MaxDepth = 1.0f) = 0;
	virtual void SetScissor(std::int32_t X, std::int32_t Y, std::uint32_t Width, std::uint32_t Height) = 0;
	virtual void BindGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) = 0;
	virtual void BindVertexBuffer(std::uint32_t Binding, FRHIBuffer* Buffer, std::uint64_t Offset = 0) = 0;
	virtual void BindIndexBuffer(FRHIBuffer* Buffer, std::uint64_t Offset = 0, bool bIndex32 = true) = 0;
	virtual void Draw(std::uint32_t VertexCount, std::uint32_t InstanceCount = 1, std::uint32_t FirstVertex = 0, std::uint32_t FirstInstance = 0) = 0;
	virtual void DrawIndexed(
		std::uint32_t IndexCount,
		std::uint32_t InstanceCount = 1,
		std::uint32_t FirstIndex = 0,
		std::int32_t VertexOffset = 0,
		std::uint32_t FirstInstance = 0) = 0;

	// Indirect draw (GPU-driven pipeline)
	virtual void DrawIndirect(
		FRHIBuffer* ArgsBuffer,
		std::uint64_t ArgsOffset,
		std::uint32_t DrawCount = 1,
		std::uint32_t Stride = 0) = 0;
	virtual void DrawIndexedIndirect(
		FRHIBuffer* ArgsBuffer,
		std::uint64_t ArgsOffset,
		std::uint32_t DrawCount = 1,
		std::uint32_t Stride = 0) = 0;

	/** GPU-driven draw count: drawCount comes from a GPU-written buffer. */
	virtual void DrawIndirectCount(
		FRHIBuffer* ArgsBuffer,
		std::uint64_t ArgsOffset,
		FRHIBuffer* CountBuffer,
		std::uint64_t CountOffset,
		std::uint32_t MaxDrawCount,
		std::uint32_t Stride = 0) = 0;
	virtual void DrawIndexedIndirectCount(
		FRHIBuffer* ArgsBuffer,
		std::uint64_t ArgsOffset,
		FRHIBuffer* CountBuffer,
		std::uint64_t CountOffset,
		std::uint32_t MaxDrawCount,
		std::uint32_t Stride = 0) = 0;

	// Compute only
	virtual void BindComputePipeline(FRHIComputePipeline* Pipeline) = 0;
	virtual void Dispatch(std::uint32_t GroupCountX, std::uint32_t GroupCountY, std::uint32_t GroupCountZ) = 0;
	virtual void DispatchIndirect(
		FRHIBuffer* ArgsBuffer,
		std::uint64_t ArgsOffset) = 0;

	// Graphics + Compute
	virtual void BindDescriptorSets(
		std::uint32_t FirstSet,
		FRHIDescriptorSet* const* Sets,
		std::uint32_t Count) = 0;
	virtual void PushConstants(
		ERHIShaderStage Stages,
		std::uint32_t Offset,
		std::uint32_t Size,
		const void* Data) = 0;

	// GPU queries (occlusion / timestamp)
	virtual void BeginQuery(FRHIQueryPool* Pool, std::uint32_t QueryIndex) = 0;
	virtual void EndQuery(FRHIQueryPool* Pool, std::uint32_t QueryIndex) = 0;
	virtual void WriteTimestamp(FRHIQueryPool* Pool, std::uint32_t QueryIndex) = 0;
	virtual void ResetQueryPool(FRHIQueryPool* Pool, std::uint32_t FirstQuery, std::uint32_t QueryCount) = 0;

	// Ray tracing
	/**
	 * Build an acceleration structure (BLAS or TLAS).
	 * ScratchBuffer must be DeviceAddress + Storage flagged and large enough
	 * for the build's scratch requirements (query via the RHI if needed).
	 */
	virtual void BuildAccelerationStructure(
		FRHIAccelerationStructure* Accel,
		FRHIBuffer* ScratchBuffer,
		std::uint64_t ScratchOffset) = 0;
	/** Copy (compact / refit-result) from Src to Dst. */
	virtual void CopyAccelerationStructure(
		FRHIAccelerationStructure* Dst,
		FRHIAccelerationStructure* Src) = 0;

	struct FRHIRayTracingSbt
	{
		FRHIBuffer* SbtBuffer = nullptr;      // whole SBT buffer
		std::uint32_t RayGenOffset = 0;
		std::uint32_t RayGenStride = 0;
		std::uint32_t HitOffset = 0;
		std::uint32_t HitStride = 0;
		std::uint32_t MissOffset = 0;
		std::uint32_t MissStride = 0;
	};

	/** Launch rays; Sbt comes from FRHIRayTracingSbt (filled by CreateShaderBindingTable). */
	virtual void TraceRays(
		FRHIRayTracingPipeline* Pipeline,
		const FRHIRayTracingSbt& Sbt,
		std::uint32_t Width,
		std::uint32_t Height,
		std::uint32_t Depth = 1) = 0;
};

} // namespace Maho
