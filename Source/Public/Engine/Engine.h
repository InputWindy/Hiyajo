#pragma once

#include <Core/Assembly.h>
#include <Core/Extension.h>
#include <Engine/Layer.h>
#include <Engine/ParallelScheduler.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Engine — the loadable application root. Assembly (exported) + parallel
// drive over its dependency table. Not a singleton. Requires C++20.
// ───────────────────────────────────────────────────────────────────────

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
