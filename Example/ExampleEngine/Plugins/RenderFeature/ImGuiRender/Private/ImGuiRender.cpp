#include "ImGuiRender.h"

#include <Frame.h>
#include <ImGuiSystem.h>
#include <Log.h>
#include <Scene.h>

#include <vulkan/vulkan.h>

#include "imgui.h"
#include "imgui_impl_vulkan.h"

namespace Maho
{

struct FImGuiRenderFeature::FData
{
	bool bInitialized = false;
	bool bHasRecorded = false;
	VkDevice Device = VK_NULL_HANDLE;
	VkQueue Queue = VK_NULL_HANDLE;
	VkCommandPool CommandPool = VK_NULL_HANDLE;
	VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
	VkFence Fence = VK_NULL_HANDLE;
};

FImGuiRenderFeature::FImGuiRenderFeature()
{
	Data = std::make_unique<FData>();

	// Acquire order: after the frame feature begins the frame.
	WaitFor<IBeginRender, FFrame, IFrameBegin>();
	// Record order: draw over the scene (FScene clears first).
	WaitFor<IRender, Scene::FScene, IRender>();
	// Submit order: after FScene's clear is submitted (queue FIFO).
	WaitFor<IEndRender, Scene::FScene, IEndRender>();
	// (FFrame additionally declares IPresent waits for my IEndRender.)
}

FImGuiRenderFeature::~FImGuiRenderFeature()
{
	if (Data != nullptr && Data->bInitialized && Data->Device != VK_NULL_HANDLE)
	{
		vkWaitForFences(Data->Device, 1, &Data->Fence, VK_TRUE, UINT64_MAX);
		if (Data->Fence != VK_NULL_HANDLE)
		{
			vkDestroyFence(Data->Device, Data->Fence, nullptr);
		}
		if (Data->CommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(Data->Device, Data->CommandPool, nullptr);
		}
	}
}

void FImGuiRenderFeature::BeginRender(FRender&)
{
}

void FImGuiRenderFeature::Render(FRender& R)
{
	IRHI* RHIPtr = R.GetRHI();
	FImGuiSystem* ImGui = R.GetImGui();
	if (RHIPtr == nullptr || ImGui == nullptr)
	{
		return;
	}

	// FRender::Tick built the UI before the render graph executed, so this
	// frame's draw data is ready (same-frame, no handoff needed).
	ImDrawData* DrawData = static_cast<ImDrawData*>(ImGui->GetDrawData());
	Scene::FScene* Scene = Scene::GetScene();
	if (DrawData == nullptr || !DrawData->Valid || DrawData->CmdListsCount <= 0
		|| Scene == nullptr || !Scene->GetSceneColor().IsValid())
	{
		return;
	}
	VkImageView SceneView = RHIPtr->GetRawTextureView(Scene->GetSceneColor().GetView());
	if (SceneView == VK_NULL_HANDLE)
	{
		return;
	}

	// Lazily create our own command resources (after the first draw data exists).
	if (!Data->bInitialized)
	{
		Data->Device = RHIPtr->GetRawDevice();
		Data->Queue = RHIPtr->GetRawGraphicsQueue();

		VkCommandPoolCreateInfo PoolInfo{};
		PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		PoolInfo.queueFamilyIndex = RHIPtr->GetRawGraphicsQueueFamilyIndex();
		vkCreateCommandPool(Data->Device, &PoolInfo, nullptr, &Data->CommandPool);

		VkCommandBufferAllocateInfo AllocInfo{};
		AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		AllocInfo.commandPool = Data->CommandPool;
		AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		AllocInfo.commandBufferCount = 1;
		vkAllocateCommandBuffers(Data->Device, &AllocInfo, &Data->CommandBuffer);

		VkFenceCreateInfo FenceInfo{};
		FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		vkCreateFence(Data->Device, &FenceInfo, nullptr, &Data->Fence);
		Data->bInitialized = true;
	}

	// Record: dynamic rendering over SceneColor (Load) + ImGui draw data.
	const std::uint32_t W = RHIPtr->GetFramebufferWidth();
	const std::uint32_t H = RHIPtr->GetFramebufferHeight();

	vkWaitForFences(Data->Device, 1, &Data->Fence, VK_TRUE, UINT64_MAX);
	vkResetFences(Data->Device, 1, &Data->Fence);
	vkResetCommandBuffer(Data->CommandBuffer, 0);

	VkCommandBufferBeginInfo BeginInfo{};
	BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(Data->CommandBuffer, &BeginInfo);

	VkRenderingAttachmentInfo ColorAtt{};
	ColorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	ColorAtt.imageView = SceneView;
	ColorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;   // scene left it here
	ColorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	ColorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo RenderingInfo{};
	RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	RenderingInfo.renderArea.extent = { W, H };
	RenderingInfo.layerCount = 1;
	RenderingInfo.colorAttachmentCount = 1;
	RenderingInfo.pColorAttachments = &ColorAtt;
	vkCmdBeginRendering(Data->CommandBuffer, &RenderingInfo);
	ImGui_ImplVulkan_RenderDrawData(DrawData, Data->CommandBuffer);
	vkCmdEndRendering(Data->CommandBuffer);
	vkEndCommandBuffer(Data->CommandBuffer);

	Data->bHasRecorded = true;
}

void FImGuiRenderFeature::EndRender(FRender&)
{
	if (Data->bInitialized && Data->bHasRecorded)
	{
		VkSubmitInfo SubmitInfo{};
		SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		SubmitInfo.commandBufferCount = 1;
		SubmitInfo.pCommandBuffers = &Data->CommandBuffer;
		vkQueueSubmit(Data->Queue, 1, &SubmitInfo, Data->Fence);
		Data->bHasRecorded = false;
	}
}

} // namespace Maho

// The C export FRender looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_IMGUIRENDER_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FImGuiRenderFeature::CreateLayer();
}
