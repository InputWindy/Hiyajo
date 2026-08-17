// Generated from Resource.cplugin Dependencies — do not edit by hand.
#pragma once
#include <Engine.h>
#include <Paths.h>
#include <Name.h>

namespace Maho
{
namespace Resource
{

/** Scheduler-level dependency declaration, synced from .cplugin. */
struct FResourceSystemDependencies
{
	using FDependsPack = TDependsPack<
		TDependsOn<EEngineStage::Init, TTypeList<
			Maho::Paths::FPaths,
			Maho::Name::FNamePool
		>>
	>;
};

} // namespace Resource
} // namespace Maho
