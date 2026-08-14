#pragma once

#include <array>
#include <cstdint>

#include <imgui.h>

namespace Maho
{

/** Must match FRenderSystem::MaxFramesInFlightCap. */
inline constexpr int ImGuiDrawDataRingSlotCount = 3;

struct FImGuiFrameSlot
{
	ImDrawData DrawData;
	bool bOccupied = false;
	std::uint64_t FrameIndex = 0;

	void ReleaseOwnedLists()
	{
		for (ImDrawList* List : DrawData.CmdLists)
		{
			if (List)
			{
				IM_DELETE(List);
			}
		}
		DrawData.Clear();
		bOccupied = false;
		FrameIndex = 0;
	}
};

struct FImGuiDrawDataRing
{
	std::array<FImGuiFrameSlot, ImGuiDrawDataRingSlotCount> Slots;

	~FImGuiDrawDataRing()
	{
		for (FImGuiFrameSlot& Slot : Slots)
		{
			Slot.ReleaseOwnedLists();
		}
	}

	void ReleaseAll()
	{
		for (FImGuiFrameSlot& Slot : Slots)
		{
			Slot.ReleaseOwnedLists();
		}
	}

	void ReleaseFrame(std::uint64_t FrameIndex)
	{
		const int SlotIndex = static_cast<int>(FrameIndex % ImGuiDrawDataRingSlotCount);
		FImGuiFrameSlot& Slot = Slots[static_cast<std::size_t>(SlotIndex)];
		if (Slot.bOccupied && Slot.FrameIndex == FrameIndex)
		{
			Slot.ReleaseOwnedLists();
		}
	}

	[[nodiscard]] int CaptureFromImGui(std::uint64_t FrameIndex)
	{
		ImDrawData* Source = ImGui::GetDrawData();
		if (!Source || !Source->Valid || Source->CmdListsCount <= 0)
		{
			return -1;
		}

		const int SlotIndex = static_cast<int>(FrameIndex % ImGuiDrawDataRingSlotCount);
		FImGuiFrameSlot& Slot = Slots[static_cast<std::size_t>(SlotIndex)];
		// Free previous occupant on the game thread only (CloneOutput shares ImDrawListSharedData).
		Slot.ReleaseOwnedLists();

		Slot.DrawData.Valid = true;
		Slot.DrawData.DisplayPos = Source->DisplayPos;
		Slot.DrawData.DisplaySize = Source->DisplaySize;
		Slot.DrawData.FramebufferScale = Source->FramebufferScale;
		Slot.DrawData.OwnerViewport = Source->OwnerViewport;

		for (int ListIndex = 0; ListIndex < Source->CmdListsCount; ++ListIndex)
		{
			ImDrawList* Clone = Source->CmdLists[ListIndex]->CloneOutput();
			if (!Clone)
			{
				continue;
			}
			// CloneOutput copies buffers but leaves write pointers unset; AddDrawList asserts on them.
			Clone->_VtxCurrentIdx = static_cast<unsigned int>(Clone->VtxBuffer.Size);
			Clone->_VtxWritePtr = Clone->VtxBuffer.Data + Clone->VtxBuffer.Size;
			Clone->_IdxWritePtr = Clone->IdxBuffer.Data + Clone->IdxBuffer.Size;
			Slot.DrawData.AddDrawList(Clone);
		}

		if (Slot.DrawData.CmdListsCount <= 0)
		{
			Slot.ReleaseOwnedLists();
			return -1;
		}

		Slot.FrameIndex = FrameIndex;
		Slot.bOccupied = true;
		return SlotIndex;
	}
};

} // namespace Maho
