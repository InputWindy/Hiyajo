#pragma once

#include <Core/Assembly.h>
#include <Core/Extension.h>
#include <Core/Singleton.h>
#include <Core/Topology.h>
#include <Engine/ParallelScheduler.h>

#include <vector>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Layer — the application root AND a nested host, unified into one template.
//
// A Layer is a dynamically-installable Assembly (exports CreateExtension → may
// be instantiated many times) with a parallel drive over its own extension
// table. NOT a singleton — the host owns its instances.
//
//   Tools    — plug-in-and-play singletons (Get().xxx()) you call directly.
//   Layers   — runtime instances (CreateExtension), driven through this host's
//              scheduler by stage + visitor lambda.
//
// A Layer's FExtensions may be anything (tools + child layers); the host
// drives tools by compile-time type and child layers by runtime instance.
// Deep inheritance is explicit: a parent Layer specialises per-project needs,
// then has its own child Layer(s).
//
// Requires C++20 (scheduler concepts).
// ───────────────────────────────────────────────────────────────────────

template <typename... TExtensions>
class TLayer
	: public TExtension<TExtensions...>
	, public IAssembly
	, public Parallel::FParallelScheduler
{
public:
	using FExtensions = typename TExtension<TExtensions...>::Type;
	/** Tools = singleton extensions (derive TSingleton); Layers = assemblies. */
	using FTools = typename TFilterWhere<FExtensions, TIsSingleton>::Type;
	using FLayerTypes = typename TFilter<FExtensions, IAssembly>::Type;
	using FTags = TTypeList<>;

	/** Runtime child-layer instances, owned by this host. */
	std::vector<IAssembly*> Layers;

	/** Instantiate every child Layer type in FLayerTypes via CreateExtension(). */
	void CreateLayers()
	{
		ForEach<FLayerTypes>(FSerialTraversePolicy{}, [this](auto Tag) {
			using T = typename decltype(Tag)::Type;
			Layers.push_back(T::CreateExtension());
		});
	}
};

} // namespace Maho
