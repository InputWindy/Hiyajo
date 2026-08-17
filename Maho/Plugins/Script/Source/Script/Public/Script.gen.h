// Generated from Script.cplugin Dependencies — do not edit by hand.
#pragma once
#include <Engine.h>
#include <ResourceSystem.h>

namespace Maho
{
namespace Script
{

/** Scheduler-level dependency declaration, synced from .cplugin. */
struct FScriptSystemDependencies
{
	using FDependsPack = TDependsPack<
		TDependsOn<EEngineStage::Init, TTypeList<
			Maho::Resource::FResourceSystem
		>>
	>;
};

} // namespace Script
} // namespace Maho
