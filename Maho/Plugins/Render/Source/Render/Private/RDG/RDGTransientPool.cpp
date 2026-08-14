#include <RDG/RDGTransientPool.h>

#include <Core/Misc/Log.h>
#include <RHI/RHI.h>

namespace Maho
{

FRDGTransientPool::~FRDGTransientPool()
{
}

void FRDGTransientPool::Reset()
{
	for (FSlot& Slot : Slots)
	{
		Slot.bFree = true;
	}
}

void FRDGTransientPool::Shutdown(IRHI* RHI)
{
	if (RHI == nullptr)
	{
		return;
	}

	for (FSlot& Slot : Slots)
	{
		if (Slot.Resource == nullptr)
		{
			continue;
		}

		if (Slot.bBuffer)
		{
			RHI->DestroyBuffer(static_cast<FRHIBuffer*>(Slot.Resource));
		}
		else
		{
			RHI->DestroyTexture(static_cast<FRHITexture*>(Slot.Resource));
		}
		Slot.Resource = nullptr;
	}
	Slots.clear();
}

FRHIBuffer* FRDGTransientPool::AllocateBuffer(
	IRHI* RHI,
	const FRHIBufferDesc& Desc,
	std::uint32_t FirstUse,
	std::uint32_t LastUse)
{
	return FindOrCreateBufferSlot(RHI, Desc, FirstUse, LastUse);
}

FRHITexture* FRDGTransientPool::AllocateTexture(
	IRHI* RHI,
	const FRHITextureDesc& Desc,
	std::uint32_t FirstUse,
	std::uint32_t LastUse)
{
	return FindOrCreateTextureSlot(RHI, Desc, FirstUse, LastUse);
}

FRHIBuffer* FRDGTransientPool::FindOrCreateBufferSlot(
	IRHI* RHI,
	const FRHIBufferDesc& Desc,
	std::uint32_t FirstUse,
	std::uint32_t LastUse)
{
	std::uint64_t RequiredSize = Desc.Size;

	for (FSlot& Slot : Slots)
	{
		if (Slot.bFree && Slot.bBuffer && Slot.LastPass < FirstUse && Slot.Size >= RequiredSize)
		{
			Slot.bFree = false;
			Slot.LastPass = LastUse;
			return static_cast<FRHIBuffer*>(Slot.Resource);
		}
	}

	FRHIBuffer* NewBuf = RHI->CreateBuffer(Desc);
	if (NewBuf == nullptr)
	{
		MAHO_CORE_ERROR("FRDGTransientPool: failed to create transient buffer");
		return nullptr;
	}

	FSlot NewSlot;
	NewSlot.Resource = NewBuf;
	NewSlot.Size = RequiredSize;
	NewSlot.LastPass = LastUse;
	NewSlot.bBuffer = true;
	NewSlot.bFree = false;
	Slots.push_back(NewSlot);

	return NewBuf;
}

FRHITexture* FRDGTransientPool::FindOrCreateTextureSlot(
	IRHI* RHI,
	const FRHITextureDesc& Desc,
	std::uint32_t FirstUse,
	std::uint32_t LastUse)
{
	std::uint64_t RequiredSize =
		static_cast<std::uint64_t>(Desc.Extent.Width) *
		static_cast<std::uint64_t>(Desc.Extent.Height) *
		static_cast<std::uint64_t>(Desc.Extent.Depth) *
		static_cast<std::uint64_t>(Desc.ArrayLayers) *
		static_cast<std::uint64_t>(Desc.MipLevels) *
		4;

	for (FSlot& Slot : Slots)
	{
		if (Slot.bFree && !Slot.bBuffer && Slot.LastPass < FirstUse && Slot.Size >= RequiredSize)
		{
			Slot.bFree = false;
			Slot.LastPass = LastUse;
			return static_cast<FRHITexture*>(Slot.Resource);
		}
	}

	FRHITexture* NewTex = RHI->CreateTexture(Desc);
	if (NewTex == nullptr)
	{
		MAHO_CORE_ERROR("FRDGTransientPool: failed to create transient texture");
		return nullptr;
	}

	FSlot NewSlot;
	NewSlot.Resource = NewTex;
	NewSlot.Size = RequiredSize;
	NewSlot.LastPass = LastUse;
	NewSlot.bBuffer = false;
	NewSlot.bFree = false;
	Slots.push_back(NewSlot);

	return NewTex;
}

} // namespace Maho
