#pragma once

#include <Core/EngineBase.h>
#include <Core/Misc/Export.h>
#include <Core/Extension/World/ECS/SystemGroup.h>
#include <Render/RenderSystem.h>

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
 */
class MAHO_API FGameEngine : public FEngineBase
{
protected:
	bool PreInitialize() override;
	void Tick() override;

	/** Create the root system group that owns the game world. */
	[[nodiscard]] virtual FSystemGroup* CreateWorld()
	{
		return new FInitializationSystemGroup();
	}
};

} // namespace Maho
