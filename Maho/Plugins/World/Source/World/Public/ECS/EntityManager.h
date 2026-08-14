#pragma once

#include "WorldApi.h"
#include <ECS/Archetype.h>
#include <ECS/ComponentType.h>
#include <ECS/EntityHandle.h>
#include <ECS/Query.h>

#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

namespace Maho
{

/**
 * Core entity storage with tag-aware component management.
 * Entities live in Archetypes based on their component mask.
 * Adding/removing components defers to EndFrame for batch migration.
 */
class MAHO_WORLD_API FEntityManager
{
public:
	FEntityManager() = default;
	~FEntityManager();

	FEntityManager(const FEntityManager&) = delete;
	FEntityManager& operator=(const FEntityManager&) = delete;
	FEntityManager(FEntityManager&&) = delete;
	FEntityManager& operator=(FEntityManager&&) = delete;

	// ─── Entity lifecycle ───

	/** Create entity with no initial components. Add later via SetComponent/AddTag. */
	[[nodiscard]] FEntityHandle CreateEntity();

	/** Create entity with given component mask (no initial data). */
	[[nodiscard]] FEntityHandle CreateEntity(const ComponentMaskType& Mask);

	/** Destroy entity immediately. */
	void DestroyEntity(FEntityHandle Handle);

	[[nodiscard]] bool IsValid(FEntityHandle Handle) const;

	// ─── Data component access (immediate) ───

	template <typename T>
	[[nodiscard]] T* GetComponent(FEntityHandle Handle)
	{
		static_assert(!IsTagComponent<T>, "GetComponent: use HasTag/AddTag/RemoveTag for tag components");
		FEntityLocation Loc = LocateEntity(Handle);
		if (Loc.Chunk == nullptr)
		{
			return nullptr;
		}
		return Loc.Chunk->GetComponent<T>(Loc.Row);
	}

	template <typename T>
	[[nodiscard]] const T* GetComponent(FEntityHandle Handle) const
	{
		static_assert(!IsTagComponent<T>, "GetComponent: use HasTag/AddTag/RemoveTag for tag components");
		FEntityLocation Loc = LocateEntity(Handle);
		if (Loc.Chunk == nullptr)
		{
			return nullptr;
		}
		return Loc.Chunk->GetComponent<T>(Loc.Row);
	}

	// ─── Data component add/remove (immediate set, deferred add/remove) ───

	/** Set component value. If entity lacks T, it is added immediately via migration. */
	template <typename T>
	void SetComponent(FEntityHandle Handle, const T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T>, "ECS component must be trivially copyable");
		static_assert(!IsTagComponent<T>, "SetComponent: use AddTag/RemoveTag for tag components");

		ComponentMaskType NewMask = GetComponentMask(Handle);
		NewMask.set(GetComponentTypeId<T>());
		FArchetype* ToArchetype = FindOrCreateArchetype(NewMask);
		if (ToArchetype == nullptr)
		{
			return;
		}

		// Serialize component value into a byte buffer for migration
		std::vector<char> CompData(sizeof(T));
		std::memcpy(CompData.data(), &Value, sizeof(T));

		MigrateEntityInternal(Handle, ToArchetype, CompData);
	}

	/** Add a component with initial value. Deferred: entity migrates on EndFrame. */
	template <typename T>
	void AddComponent(FEntityHandle Handle, const T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T>, "ECS component must be trivially copyable");
		static_assert(!IsTagComponent<T>, "AddComponent: use AddTag for tag components");
		PendingAdds.push_back({Handle, MakeComponentMask<T>(), sizeof(T), nullptr});
		std::size_t Slot = PendingAdds.size() - 1;
		void* Data = ::operator new(sizeof(T));
		std::memcpy(Data, &Value, sizeof(T));
		PendingAdds[Slot].ComponentData = Data;
	}

	/** Remove a component. Deferred: entity migrates on EndFrame. */
	template <typename T>
	void RemoveComponent(FEntityHandle Handle)
	{
		static_assert(!IsTagComponent<T>, "RemoveComponent: use RemoveTag for tag components");
		PendingRemoves.push_back({Handle, MakeComponentMask<T>(), 0, nullptr});
	}

	// ─── Tag component operations (immediate) ───

	template <typename T>
	[[nodiscard]] bool HasTag(FEntityHandle Handle) const
	{
		static_assert(IsTagComponent<T>, "HasTag: T must be a tag component (sizeof(T) == 0)");
		ComponentMaskType Mask = GetComponentMask(Handle);
		return Mask.test(GetComponentTypeId<T>());
	}

	template <typename T>
	void AddTag(FEntityHandle Handle)
	{
		static_assert(IsTagComponent<T>, "AddTag: T must be a tag component (sizeof(T) == 0)");
		ComponentMaskType CurrentMask = GetComponentMask(Handle);
		if (CurrentMask.test(GetComponentTypeId<T>()))
		{
			return; // already has tag
		}
		ComponentMaskType NewMask = CurrentMask;
		NewMask.set(GetComponentTypeId<T>());
		FArchetype* ToArchetype = FindOrCreateArchetype(NewMask);
		if (ToArchetype == nullptr)
		{
			return;
		}
		MigrateEntityInternal(Handle, ToArchetype, {});
	}

	template <typename T>
	void RemoveTag(FEntityHandle Handle)
	{
		static_assert(IsTagComponent<T>, "RemoveTag: T must be a tag component (sizeof(T) == 0)");
		ComponentMaskType CurrentMask = GetComponentMask(Handle);
		if (!CurrentMask.test(GetComponentTypeId<T>()))
		{
			return; // doesn't have tag
		}
		ComponentMaskType NewMask = CurrentMask;
		NewMask.reset(GetComponentTypeId<T>());
		FArchetype* ToArchetype = FindOrCreateArchetype(NewMask);
		if (ToArchetype == nullptr)
		{
			return;
		}
		MigrateEntityInternal(Handle, ToArchetype, {});
	}

	// ─── Type-erased operations (for ECB playback) ───

	/**
	 * Set a component value. If entity lacks the type, migrates to new archetype immediately.
	 * Type-erased version: caller passes raw byte data.
	 */
	void SetComponentTypeErased(FEntityHandle Handle, FComponentTypeId TypeId, const void* Data, std::size_t DataSize);

	/**
	 * Deferred add for type-erased calls. Migrates on EndFrame.
	 */
	void AddComponentTypeErased(FEntityHandle Handle, FComponentTypeId TypeId, const void* Data, std::size_t DataSize);

	/**
	 * Deferred remove for type-erased calls. Migrates on EndFrame.
	 */
	void RemoveComponentTypeErased(FEntityHandle Handle, FComponentTypeId TypeId);

	/**
	 * Add a tag via type-erased call. Immediate migration.
	 */
	void AddTagTypeErased(FEntityHandle Handle, FComponentTypeId TypeId);

	/**
	 * Remove a tag via type-erased call. Immediate migration.
	 */
	void RemoveTagTypeErased(FEntityHandle Handle, FComponentTypeId TypeId);

	// ─── Deferred operations ───

	/** Process all pending add/remove component operations. */
	void EndFrame();

	// ─── Query support ───

	/**
	 * Collect all chunks whose mask contains Required AND does not contain any of Excluded.
	 * Used by TComponentQuery.
	 */
	void GatherMatchingChunks(const ComponentMaskType& Required, const ComponentMaskType& Excluded,
	                          std::vector<FChunk*>& OutChunks) const;

	// ─── Statistics ───

	[[nodiscard]] std::size_t GetEntityCount() const { return EntityCount; }
	[[nodiscard]] std::size_t GetArchetypeCount() const { return Archetypes.size(); }

	/** Get the component mask for a given entity. */
	[[nodiscard]] ComponentMaskType GetComponentMask(FEntityHandle Handle) const
	{
		FEntityLocation Loc = LocateEntity(Handle);
		if (Loc.Chunk == nullptr || Loc.ArchetypeIndex >= Archetypes.size())
		{
			return {};
		}
		return Archetypes[Loc.ArchetypeIndex].Archetype->Mask;
	}

	/** Iterate all alive entities. */
	template <typename F>
	void ForEachEntity(F&& Func) const
	{
		for (std::size_t I = 0; I < EntitySlots.size(); ++I)
		{
			const FEntitySlot& Slot = EntitySlots[I];
			if (Slot.bAlive)
			{
				Func(FEntityHandle::Make(static_cast<std::uint32_t>(I), Slot.Generation));
			}
		}
	}

private:
	struct FEntityLocation
	{
		FChunk* Chunk = nullptr;
		std::size_t Row = 0;
		std::size_t ArchetypeIndex = 0;
	};

	struct FDeferredOp
	{
		FEntityHandle Handle;
		ComponentMaskType Mask;
		std::size_t ComponentSize = 0;
		void* ComponentData = nullptr;
	};

	struct FArchetypeEntry
	{
		FArchetype* Archetype = nullptr;
	};

	struct FEntitySlot
	{
		FChunk* Chunk = nullptr;
		std::size_t Row = 0;
		std::size_t ArchetypeIndex = 0;
		std::uint8_t Generation = 0;
		bool bAlive = false;
	};

	FEntityLocation LocateEntity(FEntityHandle Handle) const;
	FArchetype* FindOrCreateArchetype(const ComponentMaskType& Mask);
	void MigrateEntityInternal(FEntityHandle Handle, FArchetype* ToArchetype, const std::vector<char>& NewComponentsData);
	FEntitySlot& GetSlot(std::uint32_t Index);
	const FEntitySlot& GetSlot(std::uint32_t Index) const;

	std::vector<FEntitySlot> EntitySlots;
	std::vector<std::uint32_t> FreeIndices;
	std::size_t EntityCount = 0;

	std::vector<FArchetypeEntry> Archetypes;

	std::vector<FDeferredOp> PendingAdds;
	std::vector<FDeferredOp> PendingRemoves;
};

// Template implementations that depend on complete types.

template <typename... Ts>
void TComponentQuery<Ts...>::Gather(const FEntityManager& Manager)
{
	ComponentMaskType Required = MakeComponentMask<Ts...>();
	ComponentMaskType EffectiveExcluded = ExcludedMask;
	std::vector<FChunk*> Matching;
	Manager.GatherMatchingChunks(Required, EffectiveExcluded, Matching);
	Views.clear();
	Views.reserve(Matching.size());
	for (FChunk* C : Matching)
	{
		TChunkView<Ts...> View;
		View.Chunk = C;
		Views.push_back(View);
	}
}

} // namespace Maho
