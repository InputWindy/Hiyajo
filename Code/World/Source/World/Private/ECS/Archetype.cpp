#include <ECS/Archetype.h>

#include <algorithm>

namespace Maho
{

FChunk::FChunk(const ComponentMaskType& InMask, const std::vector<std::size_t>& InComponentSizes)
	: Mask(InMask)
	, ComponentSizes(InComponentSizes)
{
	// Calculate layout: entity rows first, then data component columns.
	// Tag components (size 0) are skipped.
	std::size_t EntityAreaSize = sizeof(FEntityRow) * Capacity;
	std::size_t DataAreaSize = 0;

	ColumnOffsets.resize(InComponentSizes.size(), 0);

	for (std::size_t I = 0; I < InComponentSizes.size(); ++I)
	{
		if (InComponentSizes[I] > 0)
		{
			ColumnOffsets[I] = EntityAreaSize + DataAreaSize;
			DataAreaSize += InComponentSizes[I] * Capacity;
		}
		else
		{
			ColumnOffsets[I] = static_cast<std::size_t>(-1); // marker for tag
		}
	}

	std::size_t TotalSize = EntityAreaSize + DataAreaSize;
	MaxCount = Capacity;

	Data = new std::uint8_t[TotalSize]();
	std::memset(Data, 0, TotalSize);
}

FChunk::FChunk(FChunk&& Other) noexcept
	: Mask(Other.Mask)
	, ComponentSizes(std::move(Other.ComponentSizes))
	, ColumnOffsets(std::move(Other.ColumnOffsets))
	, MaxCount(Other.MaxCount)
	, Count(Other.Count)
	, Data(Other.Data)
{
	Other.Data = nullptr;
	Other.Count = 0;
	Other.MaxCount = 0;
}

FChunk& FChunk::operator=(FChunk&& Other) noexcept
{
	if (this != &Other)
	{
		delete[] Data;
		Mask = Other.Mask;
		ComponentSizes = std::move(Other.ComponentSizes);
		ColumnOffsets = std::move(Other.ColumnOffsets);
		MaxCount = Other.MaxCount;
		Count = Other.Count;
		Data = Other.Data;
		Other.Data = nullptr;
		Other.Count = 0;
		Other.MaxCount = 0;
	}
	return *this;
}

FChunk::~FChunk()
{
	delete[] Data;
	Data = nullptr;
}

void* FChunk::GetComponentColumn(std::size_t TypeIndex)
{
	if (TypeIndex >= ColumnOffsets.size())
	{
		return nullptr;
	}
	std::size_t Offset = ColumnOffsets[TypeIndex];
	if (Offset == static_cast<std::size_t>(-1))
	{
		return nullptr; // tag column
	}
	return Data + Offset;
}

const void* FChunk::GetComponentColumn(std::size_t TypeIndex) const
{
	if (TypeIndex >= ColumnOffsets.size())
	{
		return nullptr;
	}
	std::size_t Offset = ColumnOffsets[TypeIndex];
	if (Offset == static_cast<std::size_t>(-1))
	{
		return nullptr; // tag column
	}
	return Data + Offset;
}

// --- FArchetype ---

FArchetype::FArchetype(const ComponentMaskType& InMask, const std::vector<std::size_t>& InComponentSizes)
	: Mask(InMask)
	, ComponentSizes(InComponentSizes)
{
}

FArchetype::~FArchetype()
{
	for (FChunk* C : Chunks)
	{
		delete C;
	}
	Chunks.clear();
}

FChunk* FArchetype::AllocateChunk()
{
	FChunk* NewChunk = new FChunk(Mask, ComponentSizes);
	Chunks.push_back(NewChunk);
	return NewChunk;
}

} // namespace Maho
