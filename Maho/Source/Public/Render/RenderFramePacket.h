#pragma once

#include <Render/SceneUpdatePacket.h>
#include <Render/ResourceSnapshots.h>
#include <Render/Sequencer/RenderFeature.h>

#include <cstdint>

namespace Maho
{

struct FRenderFramePacket
{
	std::uint64_t FrameIndex = 0;
	float ClearColorR = 0.f;
	float ClearColorG = 0.f;
	float ClearColorB = 0.f;
	float ClearColorA = 1.f;
	int ImGuiSlotIndex = -1;
	bool bSubmitImGui = false;
	bool bSubmitImGuiViewports = false;
	int FramebufferWidth = 0;
	int FramebufferHeight = 0;
	bool bResizeFramebuffer = false;
	FGameFrameContext FrameContext;
};

} // namespace Maho