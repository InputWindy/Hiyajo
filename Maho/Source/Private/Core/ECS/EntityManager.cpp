#include <Core/Extension/World/ECS/EntityManager.h>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace Maho
{

FEntityManager::~FEntityManager()
{
	for (auto& Entry : Archetypes)
	{
		delete Entry.Archetype;
	}
	Archetypes.clear();

	for (auto& Op : PendingAdds)
	{
		if (Op.ComponentData)
		{
			::operator delete(Op.ComponentData);
		}
	}
	PendingAdds.clear();
}

// --- Entity lifecycle ---

FEntityHandle FEntityManager::CreateEntity()
{
	return CreateEntity(ComponentMaskType{});
}

FEntityHandle FEntityManager::CreateEntity(const ComponentMaskType& Mask)
{
	FEntityHandle Handle;

	if (!FreeIndices.empty())
	{
		std::uint32_t Index = FreeIndices.back();
		FreeIndices.pop_back();
		FEntitySlot& Slot = EntitySlots[Index];
		Slot.Generation++;
		Slot.bAlive = true;
		Handle = FEntityHandle::Make(Index, Slot.Generation);
	}
	else
	{
		std::uint32_t Index = static_cast<std::uint32_t>(EntitySlots.size());
		EntitySlots.emplace_back();
		FEntitySlot& Slot = EntitySlots.back();
		Slot.Generation = 1;
		Slot.bAlive = true;
		Handle = FEntityHandle::Make(Index, 1);
	}

	if (Mask.any())
	{
		FArchetype* Archetype = FindOrCreateArchetype(Mask);
		if (Archetype != nullptr)
		{
			// Find or allocate a chunk with space
			FChunk* TargetChunk = nullptr;
			for (FChunk* C : Archetype->Chunks)
			{
				if (!C->IsFull())
				{
					TargetChunk = C;
					break;
				}
			}
			if (TargetChunk == nullptr)
			{
				TargetChunk = Archetype->AllocateChunk();
			}

			std::size_t Row = TargetChunk->Count;
			TargetChunk->GetEntityRows()[Row].Handle = Handle;
			TargetChunk->Count++;

			std::size_t ArchetypeIndex = 0;
			for (std::size_t I = 0; I < Archetypes.size(); ++I)
			{
				if (Archetypes[I].Archetype == Archetype)
				{
					ArchetypeIndex = I;
					break;
				}
			}

			FEntitySlot& Slot = GetSlot(Handle.Index);
			Slot.Chunk = TargetChunk;
			Slot.Row = Row;
			Slot.ArchetypeIndex = ArchetypeIndex;
		}
	}

	EntityCount++;
	return Handle;
}

void FEntityManager::DestroyEntity(FEntityHandle Handle)
{
	if (!IsValid(Handle))
	{
		return;
	}

	FEntitySlot& Slot = GetSlot(Handle.Index);
	FChunk* Chunk = Slot.Chunk;
	std::size_t Row = Slot.Row;

	// Swap-remove from chunk
	if (Chunk != nullptr && Chunk->Count > 1 && Row < Chunk->Count - 1)
	{
		std::size_t LastRow = Chunk->Count - 1;

		// Copy entity row
		Chunk->GetEntityRows()[Row] = Chunk->GetEntityRows()[LastRow];
		FEntityHandle MovedHandle = Chunk->GetEntityRows()[Row].Handle;

		// Update moved entity's slot
		FEntitySlot& MovedSlot = GetSlot(MovedHandle.Index);
		MovedSlot.Row = Row;

		// Copy component data for all data columns
		const ComponentMaskType& M = Chunk->Mask;
		for (std::size_t TypeIdx = 0; TypeIdx < ECSMaxComponentTypes; ++TypeIdx)
		{
			if (M.test(TypeIdx))
			{
				std::size_t CompSize = Internal::GetComponentSize(static_cast<FComponentTypeId>(TypeIdx));
				if (CompSize > 0)
				{
					void* Col = Chunk->GetComponentColumn(TypeIdx);
					if (Col)
					{
						std::memcpy(static_cast<char*>(Col) + Row * CompSize,
						            static_cast<const char*>(Col) + LastRow * CompSize,
						            CompSize);
					}
				}
			}
		}
	}

	if (Chunk != nullptr)
	{
		Chunk->Count--;
	}

	// Invalidate slot
	Slot.bAlive = false;
	Slot.Chunk = nullptr;
	Slot.Row = 0;
	Slot.ArchetypeIndex = 0;
	FreeIndices.push_back(Handle.Index);
	EntityCount--;
}

bool FEntityManager::IsValid(FEntityHandle Handle) const
{
	if (!Handle.IsValid() || Handle.Index >= EntitySlots.size())
	{
		return false;
	}
	const FEntitySlot& Slot = EntitySlots[Handle.Index];
	return Slot.bAlive && Slot.Generation == Handle.Generation;
}

// --- Deferred operations ---

void FEntityManager::EndFrame()
{
	// Process adds
	for (auto& Op : PendingAdds)
	{
		if (!IsValid(Op.Handle))
		{
			if (Op.ComponentData)
			{
				::operator delete(Op.ComponentData);
			}
			continue;
		}

		ComponentMaskType CurrentMask = GetComponentMask(Op.Handle);
		ComponentMaskType NewMask = CurrentMask | Op.Mask;

		if (NewMask == CurrentMask)
		{
			if (Op.ComponentData)
			{
				::operator delete(Op.ComponentData);
			}
			continue;
		}

		FArchetype* ToArchetype = FindOrCreateArchetype(NewMask);
		if (ToArchetype == nullptr)
		{
			if (Op.ComponentData)
			{
				::operator delete(Op.ComponentData);
			}
			continue;
		}

		std::vector<char> NewData;
		if (Op.ComponentData && Op.ComponentSize > 0)
		{
			NewData.resize(Op.ComponentSize);
			std::memcpy(NewData.data(), Op.ComponentData, Op.ComponentSize);
		}

		MigrateEntityInternal(Op.Handle, ToArchetype, NewData);

		if (Op.ComponentData)
		{
			::operator delete(Op.ComponentData);
		}
	}
	PendingAdds.clear();

	// Process removes
	for (auto& Op : PendingRemoves)
	{
		if (!IsValid(Op.Handle))
		{
			continue;
		}

		ComponentMaskType CurrentMask = GetComponentMask(Op.Handle);
		ComponentMaskType NewMask = CurrentMask & ~Op.Mask;

		if (NewMask == CurrentMask)
		{
			continue;
		}

		FArchetype* ToArchetype = FindOrCreateArchetype(NewMask);
		if (ToArchetype == nullptr)
		{
			if (NewMask.none())
			{
				// Remove from archetype, entity becomes componentless
				MigrateEntityInternal(Op.Handle, ToArchetype, {});
			}
			continue;
		}

		MigrateEntityInternal(Op.Handle, ToArchetype, {});
	}
	PendingRemoves.clear();
}

// --- Query support ---

void FEntityManager::GatherMatchingChunks(const ComponentMaskType& Required,
                                           const ComponentMaskType& Excluded,
                                           std::vector<FChunk*>& OutChunks) const
{
	for (const auto& Entry : Archetypes)
	{
		if (Entry.Archetype == nullptr)
		{
			continue;
		}

		const ComponentMaskType& ArchetypeMask = Entry.Archetype->Mask;

		// ArchetypeMask must contain all Required bits
		if ((ArchetypeMask & Required) != Required)
		{
			continue;
		}

		// ArchetypeMask must NOT contain any Excluded bits
		if ((ArchetypeMask & Excluded).any())
		{
			continue;
		}

		for (FChunk* C : Entry.Archetype->Chunks)
		{
			if (!C->IsEmpty())
			{
				OutChunks.push_back(C);
			}
		}
	}
}

// --- Type-erased operations ---

void FEntityManager::SetComponentTypeErased(FEntityHandle Handle, FComponentTypeId TypeId,
                                              const void* Data, std::size_t DataSize)
{
	if (!IsValid(Handle) || Data == nullptr || DataSize == 0)
	{
		return;
	}

	ComponentMaskType NewMask = GetComponentMask(Handle);
	NewMask.set(TypeId);

	FArchetype* ToArchetype = FindOrCreateArchetype(NewMask);
	if (ToArchetype == nullptr)
	{
		return;
	}

	std::vector<char> CompData(DataSize);
	std::memcpy(CompData.data(), Data, DataSize);

	MigrateEntityInternal(Handle, ToArchetype, CompData);
}

void FEntityManager::AddComponentTypeErased(FEntityHandle Handle, FComponentTypeId TypeId,
                                              const void* Data, std::size_t DataSize)
{
	if (!IsValid(Handle) || Data == nullptr || DataSize == 0)
	{
		return;
	}
	void* Stored = ::operator new(DataSize);
	std::memcpy(Stored, Data, DataSize);

	ComponentMaskType TypeMask;
	TypeMask.set(TypeId);

	PendingAdds.push_back({Handle, TypeMask, DataSize, Stored});
}

void FEntityManager::RemoveComponentTypeErased(FEntityHandle Handle, FComponentTypeId TypeId)
{
	if (!IsValid(Handle))
	{
		return;
	}
	ComponentMaskType TypeMask;
	TypeMask.set(TypeId);

	PendingRemoves.push_back({Handle, TypeMask, 0, nullptr});
}

void FEntityManager::AddTagTypeErased(FEntityHandle Handle, FComponentTypeId TypeId)
{
	if (!IsValid(Handle))
	{
		return;
	}

	ComponentMaskType CurrentMask = GetComponentMask(Handle);
	if (CurrentMask.test(TypeId))
	{
		return;
	}

	ComponentMaskType NewMask = CurrentMask;
	NewMask.set(TypeId);

	FArchetype* ToArchetype = FindOrCreateArchetype(NewMask);
	if (ToArchetype == nullptr)
	{
		return;
	}

	MigrateEntityInternal(Handle, ToArchetype, {});
}

void FEntityManager::RemoveTagTypeErased(FEntityHandle Handle, FComponentTypeId TypeId)
{
	if (!IsValid(Handle))
	{
		return;
	}

	ComponentMaskType CurrentMask = GetComponentMask(Handle);
	if (!CurrentMask.test(TypeId))
	{
		return;
	}

	ComponentMaskType NewMask = CurrentMask;
	NewMask.reset(TypeId);

	FArchetype* ToArchetype = FindOrCreateArchetype(NewMask);
	if (ToArchetype == nullptr)
	{
		return;
	}

	MigrateEntityInternal(Handle, ToArchetype, {});
}

// --- Internal helpers ---

FEntityManager::FEntityLocation FEntityManager::LocateEntity(FEntityHandle Handle) const
{
	FEntityLocation Loc;
	if (!Handle.IsValid() || Handle.Index >= EntitySlots.size())
	{
		return Loc;
	}

	const FEntitySlot& Slot = EntitySlots[Handle.Index];
	if (!Slot.bAlive || Slot.Generation != Handle.Generation)
	{
		return Loc;
	}

	Loc.Chunk = Slot.Chunk;
	Loc.Row = Slot.Row;
	Loc.ArchetypeIndex = Slot.ArchetypeIndex;
	return Loc;
}

FArchetype* FEntityManager::FindOrCreateArchetype(const ComponentMaskType& Mask)
{
	for (auto& Entry : Archetypes)
	{
		if (Entry.Archetype != nullptr && Entry.Archetype->MatchesMask(Mask))
		{
			return Entry.Archetype;
		}
	}

	// Compute component sizes from registry
	std::vector<std::size_t> Sizes(ECSMaxComponentTypes, 0);
	for (std::size_t I = 0; I < ECSMaxComponentTypes; ++I)
	{
		if (Mask.test(I))
		{
			Sizes[I] = Internal::GetComponentSize(static_cast<FComponentTypeId>(I));
		}
	}

	FArchetype* NewArch = new FArchetype(Mask, Sizes);
	FEntityManager::FArchetypeEntry Entry;
	Entry.Archetype = NewArch;
	Archetypes.push_back(Entry);

	return NewArch;
}

void FEntityManager::MigrateEntityInternal(FEntityHandle Handle, FArchetype* ToArchetype,
                                             const std::vector<char>& NewComponentsData)
{
	if (!IsValid(Handle) || ToArchetype == nullptr)
	{
		return;
	}

	FEntitySlot& Slot = GetSlot(Handle.Index);
	FChunk* FromChunk = Slot.Chunk;
	std::size_t FromRow = Slot.Row;

	// Find target archetype index
	std::size_t ToArchIndex = 0;
	for (std::size_t I = 0; I < Archetypes.size(); ++I)
	{
		if (Archetypes[I].Archetype == ToArchetype)
		{
			ToArchIndex = I;
			break;
		}
	}

	// Allocate space in target archetype
	FChunk* ToChunk = nullptr;
	for (FChunk* C : ToArchetype->Chunks)
	{
		if (!C->IsFull())
		{
			ToChunk = C;
			break;
		}
	}
	if (ToChunk == nullptr)
	{
		ToChunk = ToArchetype->AllocateChunk();
	}

	std::size_t ToRow = ToChunk->Count;

	// Copy entity handle
	ToChunk->GetEntityRows()[ToRow].Handle = Handle;

	// Copy all component data from old location (if exists) to new location
	// Only copy components that exist in BOTH archetypes
	if (FromChunk != nullptr)
	{
		const ComponentMaskType& FromMask = FromChunk->Mask;
		const ComponentMaskType& ToMask = ToChunk->Mask;

		for (std::size_t TypeIdx = 0; TypeIdx < ECSMaxComponentTypes; ++TypeIdx)
		{
			if (FromMask.test(TypeIdx) && ToMask.test(TypeIdx))
			{
				std::size_t CompSize = Internal::GetComponentSize(static_cast<FComponentTypeId>(TypeIdx));
				if (CompSize > 0)
				{
					const void* FromCol = FromChunk->GetComponentColumn(TypeIdx);
					void* ToCol = ToChunk->GetComponentColumn(TypeIdx);
					if (FromCol && ToCol)
					{
						std::memcpy(static_cast<char*>(ToCol) + ToRow * CompSize,
						            static_cast<const char*>(FromCol) + FromRow * CompSize,
						            CompSize);
					}
				}
			}
		}
	}

	// Copy new component data
	if (!NewComponentsData.empty() && ToChunk->Mask.any())
	{
		// Find which bits were added (in ToMask but not in FromMask)
		ComponentMaskType FromMask;
		if (FromChunk != nullptr)
		{
			FromMask = FromChunk->Mask;
		}
		ComponentMaskType AddedMask = ToChunk->Mask & ~FromMask;

		std::size_t NewDataOffset = 0;
		if (AddedMask.any())
		{
			for (std::size_t TypeIdx = 0; TypeIdx < ECSMaxComponentTypes; ++TypeIdx)
			{
				if (AddedMask.test(TypeIdx))
				{
					std::size_t CompSize = Internal::GetComponentSize(static_cast<FComponentTypeId>(TypeIdx));
					if (CompSize > 0 && NewDataOffset < NewComponentsData.size())
					{
						void* ToCol = ToChunk->GetComponentColumn(TypeIdx);
						if (ToCol)
						{
							std::size_t CopySize = std::min(CompSize, NewComponentsData.size() - NewDataOffset);
							std::memcpy(static_cast<char*>(ToCol) + ToRow * CompSize,
							            NewComponentsData.data() + NewDataOffset,
							            CopySize);
							NewDataOffset += CopySize;
						}
					}
				}
			}
		}
	}

	ToChunk->Count++;

	// Remove from old chunk (swap-remove)
	if (FromChunk != nullptr)
	{
		if (FromChunk->Count > 1 && FromRow < FromChunk->Count - 1)
		{
			std::size_t LastRow = FromChunk->Count - 1;

			FromChunk->GetEntityRows()[FromRow] = FromChunk->GetEntityRows()[LastRow];
			FEntityHandle MovedHandle = FromChunk->GetEntityRows()[FromRow].Handle;

			FEntitySlot& MovedSlot = GetSlot(MovedHandle.Index);
			MovedSlot.Row = FromRow;

			const ComponentMaskType& M = FromChunk->Mask;
			for (std::size_t TypeIdx = 0; TypeIdx < ECSMaxComponentTypes; ++TypeIdx)
			{
				if (M.test(TypeIdx))
				{
					std::size_t CompSize = Internal::GetComponentSize(static_cast<FComponentTypeId>(TypeIdx));
					if (CompSize > 0)
					{
						void* Col = FromChunk->GetComponentColumn(TypeIdx);
						if (Col)
						{
							std::memcpy(static_cast<char*>(Col) + FromRow * CompSize,
							            static_cast<const char*>(Col) + LastRow * CompSize,
							            CompSize);
						}
					}
				}
			}
		}
		FromChunk->Count--;
	}

	// Update slot
	Slot.Chunk = ToChunk;
	Slot.Row = ToRow;
	Slot.ArchetypeIndex = ToArchIndex;
}

FEntityManager::FEntitySlot& FEntityManager::GetSlot(std::uint32_t Index)
{
	if (Index >= EntitySlots.size())
	{
		EntitySlots.resize(Index + 1);
	}
	return EntitySlots[Index];
}

const FEntityManager::FEntitySlot& FEntityManager::GetSlot(std::uint32_t Index) const
{
	return EntitySlots[Index];
}

} // namespace Maho
