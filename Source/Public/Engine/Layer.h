#pragma once

#include <Core/Assembly.h>
#include <Core/Topology.h>
#include <Engine/ParallelScheduler.h>

#include <vector>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Layer — the installable application root (and a nested host). A dynamically
// installable Assembly (exports CreateExtension → may be instantiated many
// times) with a parallel drive. NOT a singleton — the host owns instances.
//
// Its extension scan table (the Tools / child Layers it schedules) is passed
// to the scheduler as FExtensions, so
//   Query<Layer::FExtensionList>().Select<IAssembly>()   // the child Layers
//   Query<Layer::FExtensionList>().Select<ISingleton>()  // the Tools it uses
// just work. No IExtension base — identity is ISingleton / IAssembly.
//
// Requires C++20 (scheduler concepts).
// ───────────────────────────────────────────────────────────────────────

template <typename... TExtensions>
class TLayer
	: public IAssembly
	, public Parallel::FParallelScheduler<TTypeList<TExtensions...>>
{
};

} // namespace Maho
