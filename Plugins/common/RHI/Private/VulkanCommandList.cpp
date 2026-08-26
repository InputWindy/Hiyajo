#include "VulkanCommandList.h"

#include "VulkanResources.h"

#include <Log.h>

#include <vector>

namespace Maho
{

namespace
{

[[nodiscard]] VkPipelineStageFlags ToVkPipelineStage(ERHIResourceState State)
{
	switch (State)
	{
	case ERHIResourceState::VertexBuffer:
		return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	case ERHIResourceState::IndexBuffer:
		return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	case ERHIResourceState::UniformBuffer:
		return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	case ERHIResourceState::ShaderResource:
		return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	case ERHIResourceState::UnorderedAccess:
		return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	case ERHIResourceState::IndirectArgument:
		return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
	case ERHIResourceState::RenderTarget:
		return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	case ERHIResourceState::DepthWrite:
		return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	case ERHIResourceState::CopySrc:
	case ERHIResourceState::CopyDst:
		return VK_PIPELINE_STAGE_TRANSFER_BIT;
	case ERHIResourceState::Present:
		return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	default:
		return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}
}

[[nodiscard]] VkAccessFlags ToVkAccess(ERHIResourceState State)
{
	switch (State)
	{
	case ERHIResourceState::VertexBuffer:
		return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	case ERHIResourceState::IndexBuffer:
		return VK_ACCESS_INDEX_READ_BIT;
	case ERHIResourceState::UniformBuffer:
		return VK_ACCESS_UNIFORM_READ_BIT;
	case ERHIResourceState::ShaderResource:
		return VK_ACCESS_SHADER_READ_BIT;
	case ERHIResourceState::UnorderedAccess:
		return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	case ERHIResourceState::IndirectArgument:
		return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	case ERHIResourceState::RenderTarget:
		return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	case ERHIResourceState::DepthWrite:
		return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	case ERHIResourceState::CopySrc:
		return VK_ACCESS_TRANSFER_READ_BIT;
	case ERHIResourceState::CopyDst:
		return VK_ACCESS_TRANSFER_WRITE_BIT;
	default:
		return 0;
	}
}

} // namespace

void FVulkanQueue::Submit(
	FRHICommandList* const* CmdLists,
	std::uint32_t Count,
	FRHISemaphore* const* WaitSemaphores,
	std::uint32_t WaitCount,
	FRHISemaphore* const* SignalSemaphores,
	std::uint32_t SignalCount,
	FRHIFence* SignalFence)
{
	if (NativeQueue == VK_NULL_HANDLE)
	{
		MAHO_LOG_CORE_ERROR("FVulkanQueue::Submit: native queue is null");
		return;
	}

	std::vector<VkCommandBuffer> VkBuffers;
	VkBuffers.reserve(Count);
	for (std::uint32_t Index = 0; Index < Count; ++Index)
	{
		auto* VulkanCL = static_cast<FVulkanCommandList*>(CmdLists[Index]);
		if (VulkanCL == nullptr || VulkanCL->GetVkCommandBuffer() == VK_NULL_HANDLE)
		{
			MAHO_LOG_CORE_ERROR("FVulkanQueue::Submit: invalid command list");
			return;
		}
		VkBuffers.push_back(VulkanCL->GetVkCommandBuffer());
	}

	std::vector<VkSemaphore> WaitVk;
	std::vector<VkPipelineStageFlags> WaitStages;
	WaitVk.reserve(WaitCount);
	WaitStages.reserve(WaitCount);
	for (std::uint32_t Index = 0; Index < WaitCount; ++Index)
	{
		auto* Sem = static_cast<FVulkanSemaphore*>(WaitSemaphores[Index]);
		if (Sem == nullptr)
		{
			continue;
		}
		WaitVk.push_back(Sem->GetVkSemaphore());
		WaitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	}

	std::vector<VkSemaphore> SignalVk;
	SignalVk.reserve(SignalCount);
	for (std::uint32_t Index = 0; Index < SignalCount; ++Index)
	{
		auto* Sem = static_cast<FVulkanSemaphore*>(SignalSemaphores[Index]);
		if (Sem == nullptr)
		{
			continue;
		}
		SignalVk.push_back(Sem->GetVkSemaphore());
	}

	VkSubmitInfo SubmitInfo{};
	SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	SubmitInfo.waitSemaphoreCount = static_cast<std::uint32_t>(WaitVk.size());
	SubmitInfo.pWaitSemaphores = WaitVk.empty() ? nullptr : WaitVk.data();
	SubmitInfo.pWaitDstStageMask = WaitStages.empty() ? nullptr : WaitStages.data();
	SubmitInfo.commandBufferCount = static_cast<std::uint32_t>(VkBuffers.size());
	SubmitInfo.pCommandBuffers = VkBuffers.data();
	SubmitInfo.signalSemaphoreCount = static_cast<std::uint32_t>(SignalVk.size());
	SubmitInfo.pSignalSemaphores = SignalVk.empty() ? nullptr : SignalVk.data();

	VkFence FenceHandle = VK_NULL_HANDLE;
	if (SignalFence != nullptr)
	{
		FenceHandle = static_cast<FVulkanFence*>(SignalFence)->GetVkFence();
	}

	const VkResult Result = vkQueueSubmit(NativeQueue, 1, &SubmitInfo, FenceHandle);
	if (Result != VK_SUCCESS)
	{
		MAHO_LOG_CORE_ERROR("FVulkanQueue::Submit: vkQueueSubmit failed ({})", static_cast<int>(Result));
	}
}

FVulkanCommandList::~FVulkanCommandList()
{
	// Command buffers are freed with their pool by FVulkanRHI.
	Buffer = VK_NULL_HANDLE;
	Pool = VK_NULL_HANDLE;
}

void FVulkanCommandList::AssertType(ERHICommandListType Allowed) const
{
	assert(Type == Allowed && "FRHICommandList type mismatch");
	(void)Allowed;
}

void FVulkanCommandList::AssertNotTransfer() const
{
	assert(Type != ERHICommandListType::Transfer && "Command not valid on Transfer command list");
}

void FVulkanCommandList::Begin()
{
	VkCommandBufferBeginInfo BeginInfo{};
	BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkResetCommandBuffer(Buffer, 0);
	vkBeginCommandBuffer(Buffer, &BeginInfo);
	bRecording = true;
}

void FVulkanCommandList::End()
{
	vkEndCommandBuffer(Buffer);
	bRecording = false;
}

void FVulkanCommandList::CopyBuffer(FRHIBuffer* Src, std::uint64_t SrcOffset, FRHIBuffer* Dst, std::uint64_t DstOffset, std::uint64_t Size)
{
	auto* SrcVk = static_cast<FVulkanBuffer*>(Src);
	auto* DstVk = static_cast<FVulkanBuffer*>(Dst);
	if (SrcVk == nullptr || DstVk == nullptr)
	{
		return;
	}

	VkBufferCopy Region{};
	Region.srcOffset = SrcOffset;
	Region.dstOffset = DstOffset;
	Region.size = Size;
	vkCmdCopyBuffer(Buffer, SrcVk->GetVkBuffer(), DstVk->GetVkBuffer(), 1, &Region);
}

void FVulkanCommandList::CopyBufferToTexture(FRHIBuffer* Src, FRHITexture* Dst, std::uint64_t SrcOffset)
{
	auto* SrcVk = static_cast<FVulkanBuffer*>(Src);
	auto* DstVk = static_cast<FVulkanTexture*>(Dst);
	if (SrcVk == nullptr || DstVk == nullptr || !bRecording)
	{
		return;
	}

	const FRHITextureDesc& Desc = DstVk->GetDesc();
	VkBufferImageCopy Region{};
	Region.bufferOffset = SrcOffset;
	Region.bufferRowLength = 0;
	Region.bufferImageHeight = 0;
	Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Region.imageSubresource.mipLevel = 0;
	Region.imageSubresource.baseArrayLayer = 0;
	Region.imageSubresource.layerCount = Desc.ArrayLayers;
	Region.imageOffset = { 0, 0, 0 };
	Region.imageExtent = { Desc.Extent.Width, Desc.Extent.Height, Desc.Extent.Depth };

	vkCmdCopyBufferToImage(
		Buffer,
		SrcVk->GetVkBuffer(),
		DstVk->GetVkImage(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&Region);
}

void FVulkanCommandList::CopyTextureToBuffer(FRHITexture* /*Src*/, FRHIBuffer* /*Dst*/, std::uint64_t /*DstOffset*/)
{
}

void FVulkanCommandList::FillBuffer(FRHIBuffer* InBuffer, std::uint64_t Offset, std::uint64_t Size, std::uint32_t Data)
{
	auto* Buf = static_cast<FVulkanBuffer*>(InBuffer);
	if (Buf == nullptr)
	{
		return;
	}
	vkCmdFillBuffer(Buffer, Buf->GetVkBuffer(), Offset, Size, Data);
}

void FVulkanCommandList::TransitionBuffer(FRHIBuffer* InBuffer, ERHIResourceState OldState, ERHIResourceState NewState)
{
	auto* Buf = static_cast<FVulkanBuffer*>(InBuffer);
	if (Buf == nullptr)
	{
		return;
	}

	VkBufferMemoryBarrier Barrier{};
	Barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	Barrier.srcAccessMask = ToVkAccess(OldState);
	Barrier.dstAccessMask = ToVkAccess(NewState);
	Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.buffer = Buf->GetVkBuffer();
	Barrier.offset = 0;
	Barrier.size = VK_WHOLE_SIZE;

	vkCmdPipelineBarrier(
		Buffer,
		ToVkPipelineStage(OldState),
		ToVkPipelineStage(NewState),
		0,
		0, nullptr,
		1, &Barrier,
		0, nullptr);
}

void FVulkanCommandList::TransitionTexture(FRHITexture* Texture, ERHIResourceState OldState, ERHIResourceState NewState)
{
	auto* Tex = static_cast<FVulkanTexture*>(Texture);
	if (Tex == nullptr || !bRecording)
	{
		return;
	}

	auto ToLayout = [](ERHIResourceState State) -> VkImageLayout
	{
		switch (State)
		{
		case ERHIResourceState::CopySrc:
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case ERHIResourceState::CopyDst:
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		case ERHIResourceState::ShaderResource:
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		case ERHIResourceState::RenderTarget:
			return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		case ERHIResourceState::DepthWrite:
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		case ERHIResourceState::Present:
			return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		case ERHIResourceState::Common:
		default:
			return VK_IMAGE_LAYOUT_UNDEFINED;
		}
	};

	const FRHITextureDesc& Desc = Tex->GetDesc();
	VkImageMemoryBarrier Barrier{};
	Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	Barrier.oldLayout = ToLayout(OldState);
	Barrier.newLayout = ToLayout(NewState);
	Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.image = Tex->GetVkImage();
	Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Barrier.subresourceRange.baseMipLevel = 0;
	Barrier.subresourceRange.levelCount = Desc.MipLevels;
	Barrier.subresourceRange.baseArrayLayer = 0;
	Barrier.subresourceRange.layerCount = Desc.ArrayLayers;
	Barrier.srcAccessMask = ToVkAccess(OldState);
	Barrier.dstAccessMask = ToVkAccess(NewState);

	vkCmdPipelineBarrier(
		Buffer,
		ToVkPipelineStage(OldState),
		ToVkPipelineStage(NewState),
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&Barrier);
}

void FVulkanCommandList::BeginRenderPass(
	FRHIRenderPass* RenderPass,
	FRHIFramebuffer* Framebuffer,
	std::uint32_t Width,
	std::uint32_t Height,
	const float ClearColor[4],
	bool bHasDepthStencil,
	float DepthClear,
	std::uint32_t StencilClear)
{
	AssertType(ERHICommandListType::Graphics);

	auto* VkPass = static_cast<FVulkanRenderPass*>(RenderPass);
	auto* VkFB = static_cast<FVulkanFramebuffer*>(Framebuffer);
	if (VkPass == nullptr || VkFB == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FVulkanCommandList::BeginRenderPass: null render pass or framebuffer");
		return;
	}

	VkClearValue ClearValues[2]{};
	std::uint32_t ClearCount = 0;

	VkClearValue& ColorClear = ClearValues[ClearCount++];
	ColorClear.color.float32[0] = ClearColor[0];
	ColorClear.color.float32[1] = ClearColor[1];
	ColorClear.color.float32[2] = ClearColor[2];
	ColorClear.color.float32[3] = ClearColor[3];

	if (bHasDepthStencil)
	{
		VkClearValue& DS = ClearValues[ClearCount++];
		DS.depthStencil.depth = DepthClear;
		DS.depthStencil.stencil = StencilClear;
	}

	VkRenderPassBeginInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	Info.renderPass = VkPass->GetVkPass();
	Info.framebuffer = VkFB->GetVkFramebuffer();
	Info.renderArea.extent = { Width, Height };
	Info.clearValueCount = ClearCount;
	Info.pClearValues = ClearValues;
	vkCmdBeginRenderPass(Buffer, &Info, VK_SUBPASS_CONTENTS_INLINE);
}

void FVulkanCommandList::EndRenderPass()
{
	AssertType(ERHICommandListType::Graphics);
	vkCmdEndRenderPass(Buffer);
}

void FVulkanCommandList::BeginRendering(
	const FRHIRenderingAttachmentInfo* ColorAttachments, std::uint32_t ColorCount,
	const FRHIRenderingAttachmentInfo* DepthAttachment,
	std::uint32_t Width, std::uint32_t Height)
{
	AssertType(ERHICommandListType::Graphics);

	std::vector<VkRenderingAttachmentInfo> ColorAtts(ColorCount);
	for (std::uint32_t I = 0; I < ColorCount; ++I)
	{
		auto* VkView = static_cast<FVulkanTextureView*>(ColorAttachments[I].View);
		if (VkView == nullptr)
		{
			continue;
		}

		VkRenderingAttachmentInfo& Att = ColorAtts[I];
		Att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		Att.imageView = VkView->GetVkImageView();
		Att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		Att.loadOp = ColorAttachments[I].LoadOp == ERHILoadOp::Clear
		             ? VK_ATTACHMENT_LOAD_OP_CLEAR
		             : VK_ATTACHMENT_LOAD_OP_LOAD;
		Att.storeOp = ColorAttachments[I].StoreOp == ERHIStoreOp::Store
		              ? VK_ATTACHMENT_STORE_OP_STORE
		              : VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Att.clearValue.color.float32[0] = ColorAttachments[I].ClearColor[0];
		Att.clearValue.color.float32[1] = ColorAttachments[I].ClearColor[1];
		Att.clearValue.color.float32[2] = ColorAttachments[I].ClearColor[2];
		Att.clearValue.color.float32[3] = ColorAttachments[I].ClearColor[3];
	}

	VkRenderingAttachmentInfo DepthAtt{};
	const VkRenderingAttachmentInfo* PDepthAtt = nullptr;
	if (DepthAttachment != nullptr && DepthAttachment->View != nullptr)
	{
		auto* VkView = static_cast<FVulkanTextureView*>(DepthAttachment->View);
		if (VkView != nullptr)
		{
			DepthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			DepthAtt.imageView = VkView->GetVkImageView();
			DepthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			DepthAtt.loadOp = DepthAttachment->LoadOp == ERHILoadOp::Clear
			                  ? VK_ATTACHMENT_LOAD_OP_CLEAR
			                  : VK_ATTACHMENT_LOAD_OP_LOAD;
			DepthAtt.storeOp = DepthAttachment->StoreOp == ERHIStoreOp::Store
			                   ? VK_ATTACHMENT_STORE_OP_STORE
			                   : VK_ATTACHMENT_STORE_OP_DONT_CARE;
			DepthAtt.clearValue.depthStencil.depth = DepthAttachment->ClearColor[0];
			DepthAtt.clearValue.depthStencil.stencil = 0;
			PDepthAtt = &DepthAtt;
		}
	}

	VkRenderingInfo Info{};
	Info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	Info.renderArea.extent = { Width, Height };
	Info.renderArea.offset = { 0, 0 };
	Info.layerCount = 1;
	Info.colorAttachmentCount = static_cast<std::uint32_t>(ColorAtts.size());
	Info.pColorAttachments = ColorAtts.empty() ? nullptr : ColorAtts.data();
	Info.pDepthAttachment = PDepthAtt;
	Info.pStencilAttachment = nullptr;
	vkCmdBeginRendering(Buffer, &Info);
}

void FVulkanCommandList::EndRendering()
{
	AssertType(ERHICommandListType::Graphics);
	vkCmdEndRendering(Buffer);
}

void FVulkanCommandList::SetViewport(float X, float Y, float Width, float Height, float MinDepth, float MaxDepth)
{
	AssertType(ERHICommandListType::Graphics);
	VkViewport Viewport{};
	Viewport.x = X;
	Viewport.y = Y;
	Viewport.width = Width;
	Viewport.height = Height;
	Viewport.minDepth = MinDepth;
	Viewport.maxDepth = MaxDepth;
	vkCmdSetViewport(Buffer, 0, 1, &Viewport);
}

void FVulkanCommandList::SetScissor(std::int32_t X, std::int32_t Y, std::uint32_t Width, std::uint32_t Height)
{
	AssertType(ERHICommandListType::Graphics);
	VkRect2D Scissor{};
	Scissor.offset = { X, Y };
	Scissor.extent = { Width, Height };
	vkCmdSetScissor(Buffer, 0, 1, &Scissor);
}

void FVulkanCommandList::BindGraphicsPipeline(FRHIGraphicsPipeline* Pipeline)
{
	AssertType(ERHICommandListType::Graphics);
	auto* VkPipeline = static_cast<FVulkanGraphicsPipeline*>(Pipeline);
	if (VkPipeline == nullptr || VkPipeline->GetVkPipeline() == VK_NULL_HANDLE)
	{
		return;
	}
	BoundGraphicsPipeline = Pipeline;
	vkCmdBindPipeline(Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, VkPipeline->GetVkPipeline());
}

void FVulkanCommandList::BindVertexBuffer(std::uint32_t Binding, FRHIBuffer* InBuffer, std::uint64_t Offset)
{
	AssertType(ERHICommandListType::Graphics);
	auto* Buf = static_cast<FVulkanBuffer*>(InBuffer);
	if (Buf == nullptr)
	{
		return;
	}
	VkBuffer VkBuf = Buf->GetVkBuffer();
	vkCmdBindVertexBuffers(Buffer, Binding, 1, &VkBuf, &Offset);
}

void FVulkanCommandList::BindIndexBuffer(FRHIBuffer* InBuffer, std::uint64_t Offset, bool bIndex32)
{
	AssertType(ERHICommandListType::Graphics);
	auto* Buf = static_cast<FVulkanBuffer*>(InBuffer);
	if (Buf == nullptr)
	{
		return;
	}
	vkCmdBindIndexBuffer(
		Buffer,
		Buf->GetVkBuffer(),
		Offset,
		bIndex32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

void FVulkanCommandList::Draw(std::uint32_t VertexCount, std::uint32_t InstanceCount, std::uint32_t FirstVertex, std::uint32_t FirstInstance)
{
	AssertType(ERHICommandListType::Graphics);
	vkCmdDraw(Buffer, VertexCount, InstanceCount, FirstVertex, FirstInstance);
}

void FVulkanCommandList::DrawIndexed(
	std::uint32_t IndexCount,
	std::uint32_t InstanceCount,
	std::uint32_t FirstIndex,
	std::int32_t VertexOffset,
	std::uint32_t FirstInstance)
{
	AssertType(ERHICommandListType::Graphics);
	vkCmdDrawIndexed(Buffer, IndexCount, InstanceCount, FirstIndex, VertexOffset, FirstInstance);
}

void FVulkanCommandList::DrawIndirect(
	FRHIBuffer* ArgsBuffer,
	std::uint64_t ArgsOffset,
	std::uint32_t DrawCount,
	std::uint32_t Stride)
{
	AssertType(ERHICommandListType::Graphics);
	auto* Buf = static_cast<FVulkanBuffer*>(ArgsBuffer);
	if (Buf == nullptr)
	{
		return;
	}
	vkCmdDrawIndirect(Buffer, Buf->GetVkBuffer(), ArgsOffset, DrawCount, Stride);
}

void FVulkanCommandList::DrawIndexedIndirect(
	FRHIBuffer* ArgsBuffer,
	std::uint64_t ArgsOffset,
	std::uint32_t DrawCount,
	std::uint32_t Stride)
{
	AssertType(ERHICommandListType::Graphics);
	auto* Buf = static_cast<FVulkanBuffer*>(ArgsBuffer);
	if (Buf == nullptr)
	{
		return;
	}
	vkCmdDrawIndexedIndirect(Buffer, Buf->GetVkBuffer(), ArgsOffset, DrawCount, Stride);
}

void FVulkanCommandList::BindComputePipeline(FRHIComputePipeline* Pipeline)
{
	AssertType(ERHICommandListType::Compute);
	auto* VkPipeline = static_cast<FVulkanComputePipeline*>(Pipeline);
	if (VkPipeline == nullptr || VkPipeline->GetVkPipeline() == VK_NULL_HANDLE)
	{
		return;
	}
	BoundComputePipeline = Pipeline;
	vkCmdBindPipeline(Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, VkPipeline->GetVkPipeline());
}

void FVulkanCommandList::Dispatch(std::uint32_t GroupCountX, std::uint32_t GroupCountY, std::uint32_t GroupCountZ)
{
	AssertType(ERHICommandListType::Compute);
	vkCmdDispatch(Buffer, GroupCountX, GroupCountY, GroupCountZ);
}

void FVulkanCommandList::DispatchIndirect(
	FRHIBuffer* ArgsBuffer,
	std::uint64_t ArgsOffset)
{
	AssertType(ERHICommandListType::Compute);
	auto* Buf = static_cast<FVulkanBuffer*>(ArgsBuffer);
	if (Buf == nullptr)
	{
		return;
	}
	vkCmdDispatchIndirect(Buffer, Buf->GetVkBuffer(), ArgsOffset);
}

void FVulkanCommandList::BindDescriptorSets(std::uint32_t FirstSet, FRHIDescriptorSet* const* Sets, std::uint32_t Count)
{
	AssertNotTransfer();

	if (Sets == nullptr || Count == 0)
	{
		return;
	}

	// Determine pipeline bind point from command list type.
	VkPipelineBindPoint BindPoint;
	switch (Type)
	{
	case ERHICommandListType::Graphics:
		BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		break;
	case ERHICommandListType::Compute:
		BindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		break;
	default:
		return;
	}

	std::vector<VkDescriptorSet> VkSets;
	VkSets.reserve(Count);
	for (std::uint32_t Index = 0; Index < Count; ++Index)
	{
		auto* VkSet = static_cast<FVulkanDescriptorSet*>(Sets[Index]);
		if (VkSet == nullptr || VkSet->GetVkSet() == VK_NULL_HANDLE)
		{
			MAHO_LOG_CORE_ERROR("FVulkanCommandList::BindDescriptorSets: null descriptor set at index {}", Index);
			return;
		}
		VkSets.push_back(VkSet->GetVkSet());
	}

	[[maybe_unused]] auto* VkGPipeline = static_cast<FVulkanGraphicsPipeline*>(BoundGraphicsPipeline);
	[[maybe_unused]] auto* VkCPipeline = static_cast<FVulkanComputePipeline*>(BoundComputePipeline);
	VkPipelineLayout Layout = VK_NULL_HANDLE;
	if (Type == ERHICommandListType::Graphics && VkGPipeline != nullptr)
	{
		Layout = VkGPipeline->GetVkPipelineLayout();
	}
	else if (Type == ERHICommandListType::Compute && VkCPipeline != nullptr)
	{
		Layout = VkCPipeline->GetVkPipelineLayout();
	}

	if (Layout == VK_NULL_HANDLE)
	{
		MAHO_LOG_CORE_ERROR("FVulkanCommandList::BindDescriptorSets: no bound pipeline to extract layout from");
		return;
	}

	vkCmdBindDescriptorSets(
		Buffer,
		BindPoint,
		Layout,
		FirstSet,
		Count,
		VkSets.data(),
		0,
		nullptr);
}

void FVulkanCommandList::PushConstants(ERHIShaderStage Stages, std::uint32_t Offset, std::uint32_t Size, const void* Data)
{
	AssertNotTransfer();

	if (Data == nullptr || Size == 0)
	{
		return;
	}

	VkShaderStageFlags VkStages = 0;
	if (RHIEnumHas(Stages, ERHIShaderStage::Vertex))
	{
		VkStages |= VK_SHADER_STAGE_VERTEX_BIT;
	}
	if (RHIEnumHas(Stages, ERHIShaderStage::Fragment))
	{
		VkStages |= VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	if (RHIEnumHas(Stages, ERHIShaderStage::Compute))
	{
		VkStages |= VK_SHADER_STAGE_COMPUTE_BIT;
	}

	if (VkStages == 0)
	{
		return;
	}

	// Determine layout from the currently bound pipeline.
	VkPipelineLayout Layout = VK_NULL_HANDLE;
	if (Type == ERHICommandListType::Graphics && BoundGraphicsPipeline != nullptr)
	{
		Layout = static_cast<FVulkanGraphicsPipeline*>(BoundGraphicsPipeline)->GetVkPipelineLayout();
	}
	else if (Type == ERHICommandListType::Compute && BoundComputePipeline != nullptr)
	{
		Layout = static_cast<FVulkanComputePipeline*>(BoundComputePipeline)->GetVkPipelineLayout();
	}

	if (Layout == VK_NULL_HANDLE)
	{
		MAHO_LOG_CORE_ERROR("FVulkanCommandList::PushConstants: no bound pipeline");
		return;
	}

	vkCmdPushConstants(Buffer, Layout, VkStages, Offset, Size, Data);
}

} // namespace Maho
