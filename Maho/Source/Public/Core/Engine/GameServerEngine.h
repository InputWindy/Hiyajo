#pragma once

#include <Core/EngineBase.h>
#include <ECS/SystemGroup.h>

#include <memory>
#include <typeindex>

namespace Maho
{

/**
 * Headless game server shell: FEngineBase plus the game world extension only.
 *
 * No render world — use for Linux dedicated servers / pure simulation that
 * never opens a window. PreInitialize registers the root FSystemGroup returned
 * by CreateWorld(); Tick() drives the game world frame and the per-frame
 * extension Tick dispatch (script / platform / resource), with no render.
 *
 * Projects subclass FGameServerEngine, override CreateWorld() to return their
 * FInitializationSystemGroup-derived world layer, and register remaining
 * extensions in PreInitialize before/after calling FGameServerEngine::PreInitialize.
 *
 * Header-only on purpose: the concrete FSystemGroup type lives in the World
 * plugin DLL, so the game EXE (which links every plugin through Maho::Modules)
 * instantiates these methods instead of Maho.dll importing plugin symbols.
 */
class FGameServerEngine : public FEngineBase
{
protected:
	bool PreInitialize() override
	{
		std::unique_ptr<FSystemGroup> World(CreateWorld());
		if (!World)
		{
			MAHO_CORE_ERROR("FGameServerEngine: CreateWorld() returned null");
			return false;
		}

		RegisterExtensionInstance(
			std::unique_ptr<IEngineExtension>(World.release()),
			std::type_index(typeid(FSystemGroup)),
			EExtensionPriority::Layer);

		return true;
	}

	void Tick() override
	{
		FSystemGroup* World = GetExtension<FSystemGroup>();

		if (World)
		{
			World->BeginFrame();
			World->Tick();
			World->EndFrame();
		}

		// Script / platform / resource per-frame hooks (no render world).
		DispatchStageToExtensions(EEngineStage::Tick);
	}

	/** Create the root system group that owns the game world. */
	[[nodiscard]] virtual FSystemGroup* CreateWorld()
	{
		return new FInitializationSystemGroup();
	}
};

} // namespace Maho
