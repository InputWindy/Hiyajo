#pragma once

#include <Core/Assembly.h>
#include <Core/Extension.h>
#include <Core/Tags.h>
#include <Core/Topology.h>
#include <Engine/ParallelScheduler.h>
#include <Engine/Tool.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Layer — the application root AND a nested host, unified into one template.
//
// A Layer is a dynamically-installable Assembly (exports CreateExtension → may
// be instantiated many times) with a parallel drive over its own extension
// table. NOT a singleton — the scheduler owns its instances.
//
//   Tools    — plug-in-and-play singletons (Get().xxx()) you call directly.
//   Layers   — heavy, driven by a Layer's scheduler (writes via ExecuteExtension).
//
// A Layer's FExtensions may be anything (tools + child layers); the scheduler
// drives them by dependency level, parallel within a level. Deep inheritance
// is explicit: a parent Layer specialises per-project needs, then has its own
// child Layer(s).
//
// Requires C++20 (scheduler concepts).
// ───────────────────────────────────────────────────────────────────────

struct FLayerTag {};

template <typename... TExtensions>
class TLayer
	: public FLayerTag
	, public TExtension<TExtensions...>
	, public IAssembly
	, public Parallel::FParallelScheduler
{
public:
	using FExtensions = typename TExtension<TExtensions...>::FExtensions;
	using FTools = typename TFilter<FExtensions, FToolTag>::Type;
	using FLayers = typename TFilter<FExtensions, FLayerTag>::Type;

	/** Identity tag FLayerTag is always present. */
	using FTags = TTypeList<FLayerTag>;

	/** Append extra tags while keeping the identity tag. */
	template <typename... TExtra>
	using WithTags = FWithTags<FTags, TExtra...>;
};

} // namespace Maho
