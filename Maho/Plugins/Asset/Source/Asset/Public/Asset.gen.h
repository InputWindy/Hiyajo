// Generated from Asset.cplugin Dependencies — do not edit by hand.
#pragma once
#include <Engine.h>
#include <Paths.h>

namespace Maho
{
namespace Asset
{

/** Scheduler-level dependency declaration, synced from .cplugin. */
struct FAssetRegistryDependencies
{
	using FDependsPack = TDependsPack<
		TDependsOn<EToolStage::Init, TTypeList<
			Maho::Paths::FPaths
		>>
	>;
};

} // namespace Asset
} // namespace Maho
