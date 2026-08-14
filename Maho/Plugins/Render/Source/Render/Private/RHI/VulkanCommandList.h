#pragma once

#include <RHI/RHICommandList.h>

#include <cassert>
#include <vulkan/vulkan.h>

namespace Maho
{

class FVulkanQueue final : public FRHIQueue
{
public:
	FVulkanQueue() = default;

	void Configure(
		ERHIQueueType InLogicalType,
		VkQueue InNativeQueue,
		std::uint32_t InFamilyIndex,
		bool bInNativeFallback)
	{
		LogicalType = InLogicalType;
		NativeQueue = InNativeQueue;
		FamilyIndex = InFamilyIndex;
		bNativeFallback = bInNativeFallback;
	}

	[[nodiscard]] ERHIQueueType GetType() const override { return LogicalType; }
	[[nodiscard]] bool IsNativeFallback() const override { return bNativeFallback; }
	[[nodiscard]] VkQueue GetVkQueue() const { return NativeQueue; }
	[[nodiscard]] std::uint32_t GetFamilyIndex() const { return FamilyIndex; }

	virtual void Submit(
		FRHICommandList* const* CmdLists,
		std::uint32_t Count,
		FRHISemaphore* const* WaitSemaphores,
		std::uint32_t WaitCount,
		FRHISemaphore* const* SignalSemaphores,
		std::uint32_t SignalCount,
		FRHIFence* SignalFence) override;

private:
	ERHIQueueType LogicalType = ERHIQueueType::Graphics;
	VkQueue NativeQueue = VK_NULL_HANDLE;
	std::uint32_t FamilyIndex = 0;
	bool bNativeFallback = false;
};

class FVulkanCommandList final : public FRHICommandList
{
public:
	FVulkanCommandList(ERHICommandListType InType, VkDevice InDevice, VkCommandPool InPool, VkCommandBuffer InBuffer)
		: Type(InType)
		, Device(InDevice)
		, Pool(InPool)
		, Buffer(InBuffer)
	{
	}

	~FVulkanCommandList() override;

	[[nodiscard]] ERHICommandListType GetType() const override { return Type; }
	[[nodiscard]] VkCommandBuffer GetVkCommandBuffer() const { return Buffer; }
	[[nodiscard]] VkCommandPool GetVkCommandPool() const { return Pool; }

	virtual void Begin() override;
	virtual void End() override;

	virtual void CopyBuffer(FRHIBuffer* Src, std::uint64_t SrcOffset, FRHIBuffer* Dst, std::uint64_t DstOffset, std::uint64_t Size) override;
	virtual void CopyBufferToTexture(FRHIBuffer* Src, FRHITexture* Dst, std::uint64_t SrcOffset) override;
	virtual void CopyTextureToBuffer(FRHITexture* Src, FRHIBuffer* Dst, std::uint64_t DstOffset) override;
	virtual void FillBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, std::uint64_t Size, std::uint32_t Data) override;
	virtual void TransitionBuffer(FRHIBuffer* Buffer, ERHIResourceState OldState, ERHIResourceState NewState) override;
	virtual void TransitionTexture(FRHITexture* Texture, ERHIResourceState OldState, ERHIResourceState NewState) override;

	virtual void BeginRenderPass(
		FRHIRenderPass* RenderPass,
		FRHIFramebuffer* Framebuffer,
		std::uint32_t Width,
		std::uint32_t Height,
		const float ClearColor[4],
		bool bHasDepthStencil,
		float DepthClear,
		std::uint32_t StencilClear) override;
	virtual void EndRenderPass() override;

	virtual void BeginRendering(
		const FRHIRenderingAttachmentInfo* ColorAttachments, std::uint32_t ColorCount,
		const FRHIRenderingAttachmentInfo* DepthAttachment,
		std::uint32_t Width, std::uint32_t Height) override;
	virtual void EndRendering() override;
	virtual void SetViewport(float X, float Y, float Width, float Height, float MinDepth, float MaxDepth) override;
	virtual void SetScissor(std::int32_t X, std::int32_t Y, std::uint32_t Width, std::uint32_t Height) override;
	virtual void BindGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) override;
	virtual void BindVertexBuffer(std::uint32_t Binding, FRHIBuffer* Buffer, std::uint64_t Offset) override;
	virtual void BindIndexBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, bool bIndex32) override;
	virtual void Draw(std::uint32_t VertexCount, std::uint32_t InstanceCount, std::uint32_t FirstVertex, std::uint32_t FirstInstance) override;
	virtual void DrawIndexed(
		std::uint32_t IndexCount,
		std::uint32_t InstanceCount,
		std::uint32_t FirstIndex,
		std::int32_t VertexOffset,
		std::uint32_t FirstInstance) override;

	virtual void DrawIndirect(
		FRHIBuffer* ArgsBuffer,
		std::uint64_t ArgsOffset,
		std::uint32_t DrawCount,
		std::uint32_t Stride) override;
	virtual void DrawIndexedIndirect(
		FRHIBuffer* ArgsBuffer,
		std::uint64_t ArgsOffset,
		std::uint32_t DrawCount,
		std::uint32_t Stride) override;

	virtual void BindComputePipeline(FRHIComputePipeline* Pipeline) override;
	virtual void Dispatch(std::uint32_t GroupCountX, std::uint32_t GroupCountY, std::uint32_t GroupCountZ) override;
	virtual void DispatchIndirect(
		FRHIBuffer* ArgsBuffer,
		std::uint64_t ArgsOffset) override;

	virtual void BindDescriptorSets(std::uint32_t FirstSet, FRHIDescriptorSet* const* Sets, std::uint32_t Count) override;
	virtual void PushConstants(ERHIShaderStage Stages, std::uint32_t Offset, std::uint32_t Size, const void* Data) override;

private:
	void AssertType(ERHICommandListType Allowed) const;
	void AssertNotTransfer() const;

	ERHICommandListType Type = ERHICommandListType::Graphics;
	VkDevice Device = VK_NULL_HANDLE;
	VkCommandPool Pool = VK_NULL_HANDLE;
	VkCommandBuffer Buffer = VK_NULL_HANDLE;
	bool bRecording = false;

	// Cached bound state for pipeline-layout dependent calls.
	FRHIGraphicsPipeline* BoundGraphicsPipeline = nullptr;
	FRHIComputePipeline* BoundComputePipeline = nullptr;
};

} // namespace Maho
