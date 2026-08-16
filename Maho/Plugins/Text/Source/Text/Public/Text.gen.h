// Generated from Text.cplugin Dependencies — do not edit by hand.
#pragma once
#include <Engine.h>

namespace Maho
{
namespace Text
{

/** Scheduler-level dependency declaration, synced from .cplugin. */
struct FTextManagerDependencies
{
	using FDependsPack = TDependsPack<
		TDependsOn<EToolStage::Init, TTypeList<
			Maho::Json::FJson
		>>
	>;
};

} // namespace Text
} // namespace Maho
