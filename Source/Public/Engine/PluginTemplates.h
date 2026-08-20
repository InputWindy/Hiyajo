#pragma once

#include <Core/Assembly.h>
#include <Core/Extension.h>
#include <Core/Runable.h>
#include <Engine/ParallelScheduler.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// The three plugin templates — how a concrete plugin derives depends on its
// role. A tool is driven by someone else; a layer drives others AND is itself
// runnable; an engine is the loadable application root.
//
//   TTool  — singleton, no scheduler (driven via ExecuteExtension)
//   TLayer — singleton + Main + parallel scheduler (a nested host)
//   TEngine— assembly + parallel scheduler (the top-level app, not singleton)
//
// FToolTag / FLayerTag mark the two "kinds" so a host can split its
// dependency list into tools vs layers for separate drives (TFilter).
// ───────────────────────────────────────────────────────────────────────

struct FToolTag {};
struct FLayerTag {};

/** Tool plugin: a singleton with a dependency table. Driven by a host. */
template <typename TDerived, typename... TExtensions>
class TTool
	: public FToolTag
	, public TExtension<TExtensions...>
	, public TSingleton<TDerived>
{
};

/** Layer plugin: a singleton host — Main + parallel drive over its own list. */
template <typename TDerived, typename... TExtensions>
class TLayer
	: public FLayerTag
	, public TExtension<TExtensions...>
	, public IRunable
	, public Parallel::FParallelScheduler
	, public TSingleton<TDerived>
{
public:
	using FExtensions = typename TExtension<TExtensions...>::FExtensions;
	using FTools = typename TFilter<FExtensions, FToolTag>::Type;
	using FLayers = typename TFilter<FExtensions, FLayerTag>::Type;
};

/** Engine plugin: the loadable application root — assembly + parallel drive. */
template <typename... TExtensions>
class TEngine
	: public TExtension<TExtensions...>
	, public IAssembly
	, public Parallel::FParallelScheduler
{
public:
	using FExtensions = typename TExtension<TExtensions...>::FExtensions;
	using FTools = typename TFilter<FExtensions, FToolTag>::Type;
	using FLayers = typename TFilter<FExtensions, FLayerTag>::Type;
};

} // namespace Maho
