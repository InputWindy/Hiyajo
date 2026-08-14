#pragma once

#include <Core/App.h>
#include <Core/Misc/Export.h>
#include <Core/Extension/World/ECS/SystemGroup.h>
#include <Render/RenderSystem.h>

namespace Maho
{

/**
 * Game application shell: FAppBase plus the two "world" extensions.
 *
 * PreInitialize registers FRenderSystem and the root FSystemGroup returned by
 * CreateWorld(). Tick() drives the render world begin, the game world frame,
 * the per-frame extension Tick dispatch (editor / script / platform), the
 * world→render gather, and finally the render world render.
 *
 * Projects subclass FGameApp, override CreateWorld() to return their
 * FInitializationSystemGroup-derived world layer, and register remaining
 * extensions in PreInitialize before/after calling FGameApp::PreInitialize.
 */
class MAHO_API FGameApp : public FAppBase
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
