#pragma once

#include <Core/EngineBase.h>
#include <ECS/SystemGroup.h>
#include <RenderSystem.h>

#include <memory>
#include <typeindex>

namespace Maho
{

/**
 * Game application shell: FEngineBase plus the two "world" extensions.
 *
 * PreInitialize registers FRenderSystem and the root FSystemGroup returned by
 * CreateWorld(). Tick() drives the render world begin, the game world frame,
 * the per-frame extension Tick dispatch (editor / script / platform), the
 * world→render gather, and finally the render world render.
 *
 * Projects subclass FGameEngine, override CreateWorld() to return their
 * FInitializationSystemGroup-derived world layer, and register remaining
 * extensions in PreInitialize before/after calling FGameEngine::PreInitialize.
 *
 * Header-only on purpose: the concrete extension types live in the Render and
 * World plugin DLLs, so the game EXE (which links every plugin through
 * Maho::Modules) instantiates these methods instead of Maho.dll importing
 * plugin symbols.
 */
class FGameEngine : public FEngineBase
{
protected:
	bool PreInitialize() override
	{
		RegisterExtension<FRenderSystem>(EExtensionPriority::System);

		std::unique_ptr<FSystemGroup> World(CreateWorld());
		if (!World)
		{
			MAHO_CORE_ERROR("FGameEngine: CreateWorld() returned null");
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
		FRenderSystem* Render = GetExtension<FRenderSystem>();
		FSystemGroup* World = GetExtension<FSystemGroup>();

		if (Render)
		{
			Render->BeginFrame();
		}

		if (World)
		{
			World->BeginFrame();
			World->Tick();
			World->EndFrame();
		}

		// Editor / script / platform / resource per-frame hooks.
		DispatchStageToExtensions(EEngineStage::Tick);

		if (Render && World)
		{
			Render->SubmitFrameContext(Render->GatherContexts(World->GetWorld()));
		}

		if (Render)
		{
			Render->RenderFrame();
		}
	}

	/** Create the root system group that owns the game world. */
	[[nodiscard]] virtual FSystemGroup* CreateWorld()
	{
		return new FInitializationSystemGroup();
	}
};

} // namespace Maho
