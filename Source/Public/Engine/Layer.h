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
// Layer — the installable application root (and a nested host). A dynamically
// installable Assembly (exports CreateExtension → may be instantiated many
// times) with a parallel drive. NOT a singleton — the host owns instances.
//
// Dependencies are declared like any extension: define using FDependsPack;
// Topology orders the types and the scheduler drives the instances.
//
// Requires C++20 (scheduler concepts).
// ───────────────────────────────────────────────────────────────────────

template <typename... TExtensions>
class TLayer
	: public IAssembly
	, public Parallel::FParallelScheduler
{
public:
	using FExtensions = TTypeList<TExtensions...>;
	using FTags = TTypeList<>;
};

} // namespace Maho
