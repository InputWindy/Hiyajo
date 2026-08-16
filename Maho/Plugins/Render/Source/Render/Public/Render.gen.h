// Generated from Render.cplugin Dependencies — do not edit by hand.
#pragma once
#include <Engine.h>

namespace Maho
{
namespace Render
{

/** Scheduler-level dependency declaration, synced from .cplugin. */
struct FRenderSystemDependencies
{
	using FDependsPack = TDependsPack<
		TDependsOn<EEngineStage::Init, TTypeList<
			Maho::Platform::FPlatformSystem
		>>
	>;
};

} // namespace Render
} // namespace Maho
