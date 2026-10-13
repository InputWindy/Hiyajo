#pragma once

#include "GameWorldApi.h"

#include <cstdint>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Maho
{
namespace GameWorld
{

/**
 * Type-erased component pool base - lets FGameWorld hold a heterogeneous set of
 * pools keyed by component type (one TComponentPool<T> per component). Removal
 * is by entity Index; a slot is only marked dead (not compacted) so the entity's
 * Index stays addressable by every other pool, matching FEntityRegistry.
 *
 * EXPORTED because the pool object crosses the module boundary that frees it: a world
 * system's module constructs the pool (through `CreateComponentPoolTyped` below, so the
 * construction itself stays in GameWorld.dll) and GAMEWORLD.DLL destroys it. The tag is
 * what keeps the deleting destructor a single symbol of this module instead of a COMDAT
 * copy emitted wherever the base is first seen - see the AGENTS export rules.
 */
class MAHO_GAMEWORLD_API IComponentPool
{
public:
	virtual ~IComponentPool() = default;

	/** Raw access to the component at Index (nullptr when not present). */
	virtual void* GetRaw(std::uint32_t Index) = 0;

	/** Mark Index dead. */
	virtual void Remove(std::uint32_t Index) = 0;

	/** Current slot capacity (high-water mark). */
	[[nodiscard]] virtual std::uint32_t Capacity() const = 0;
};

/**
 * SoA component storage for one component type. Addressable by entity Index.
 * Add resizes the column on demand and relies on MoveAssign into a slot; a slot
 * stays put (sparse, no compaction) so component lifetime aligns with the
 * entity's Index.
 */
template <typename T>
class TComponentPool : public IComponentPool
{
public:
	[[nodiscard]] T* Get(std::uint32_t Index)
	{
		if (Index < Has.size() && Has[Index])
		{
			return &Items[Index];
		}
		return nullptr;
	}

	[[nodiscard]] const T* Get(std::uint32_t Index) const
	{
		if (Index < Has.size() && Has[Index])
		{
			return &Items[Index];
		}
		return nullptr;
	}

	/** Add (or overwrite) a component at Index; returns a pointer to the stored value. */
	T* Add(std::uint32_t Index, T Value)
	{
		if (Index >= Items.size())
		{
			const std::size_t NewSize = static_cast<std::size_t>(Index) + 1;
			Items.resize(NewSize);
			Has.resize(NewSize, false);
		}
		Items[Index] = std::move(Value);
		Has[Index] = true;
		return &Items[Index];
	}

	void* GetRaw(std::uint32_t Index) override { return Get(Index); }

	void Remove(std::uint32_t Index) override
	{
		if (Index < Has.size())
		{
			Has[Index] = false;
		}
	}

	[[nodiscard]] std::uint32_t Capacity() const override { return static_cast<std::uint32_t>(Items.size()); }

private:
	std::vector<T>      Items;   // index-aligned component values
	std::vector<bool>   Has;     // index-aligned presence mask
};

/**
 * Create the component pool for `C` - **inside GameWorld.dll**, which is the point of this function
 * existing at all.
 *
 * `TComponentPool<T>` is an all-inline header TEMPLATE, so it has no key function: its vtable and
 * its deleting destructor are emitted as a COMDAT **in whichever module constructs the object**.
 * The natural spelling `std::make_unique<TComponentPool<C>>()` therefore stamps a WORLD SYSTEM's
 * vtable into the pool (`MeshRenderSystem.dll` for the render components, `UISystem.dll` for
 * `FUIWidget`), and `FGameWorld` then destroys that pool through `unique_ptr<IComponentPool>`
 * from GAMEWORLD.DLL - a call into another module's deleting destructor, which is only safe while
 * that module is mapped. It generally IS not: `FGameWorld::Shutdown` uninstalls its systems, and
 * the uninstall is what `FreeLibrary`s their modules.
 *
 * So the construction is routed through here (the definition, and therefore the instantiation,
 * lives in GameWorld.cpp), which makes the pool's vtable and deleting destructor live in the module
 * that also OWNS the pool - the same fix the export rules prescribe for any type whose instances
 * outlive the module that made them, applied to a template where a `MAHO_*_API` tag alone cannot do
 * it.
 *
 * TYPED, and the type list is the .cpp's EXPLICIT INSTANTIATIONS: no `type_info`, no cast, and a
 * component type that is used but not listed is a LINK ERROR instead of a pool that silently comes
 * back empty.
 *
 * ORDERING IS STILL REQUIRED: `FGameWorld::Shutdown` frees the pools BEFORE it uninstalls the
 * systems, because a pool's elements (e.g. `FScriptComponent`, `FUIWidget`) hold `shared_ptr`s whose
 * deleters were baked into the system modules -- `TComponentPool<T>`'s own destructor is in
 * GameWorld.dll now, but `~T` is not.
 */
template <typename C>
MAHO_GAMEWORLD_API TComponentPool<C>* CreateComponentPoolTyped();

} // namespace GameWorld
} // namespace Maho
