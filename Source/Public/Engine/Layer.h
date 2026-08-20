#pragma once

#include <Core/Extension.h>
#include <Core/Runable.h>
#include <Engine/ParallelScheduler.h>
#include <Engine/Tool.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Layer — a nested host. Singleton + Main + parallel drive over its own
// dependency table. Requires C++20 (scheduler concepts).
// ───────────────────────────────────────────────────────────────────────

struct FLayerTag {};

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

	/** Identity tag FLayerTag is always present. */
	using FTags = TTypeList<FLayerTag>;

	/** Append extra tags while keeping the identity tag. */
	template <typename... TExtra>
	using WithTags = FWithTags<FTags, TExtra...>;
};

} // namespace Maho
