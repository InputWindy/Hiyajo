#pragma once

#include <Core/Export.h>
#include <Core/TypeList.h>

#include <type_traits>

namespace Maho
{

/**
 * Capability composer -- an UNORDERED set of capability traits a class promises.
 * No stage ordering is implied; used for capability query (dynamic_cast) and
 * capability-driven dispatch.
 *
 *   class FMyEngine : public virtual IPlugin<IMain, IInit, IShutdown> {};
 */
template <typename... TCapabilities>
class MAHO_API IPlugin : public virtual TCapabilities...
{
public:
	virtual ~IPlugin() = default;
};

/**
 * Stage pipeline composer -- an ORDERED pipeline of lifecycle stages. Argument
 * order is the layer's own node order: IPipeline<IInit, IMain, IShutdown>
 * means the layer's nodes run Init -> Main -> Shutdown (auto self-progression
 * edges). Exposes TStages (an ordered TTypeList) so the TaskGraph can expand
 * one node per stage.
 *
 * The stage-invoke protocol is implemented by the CONCRETE pipeline class
 * (each concrete pipeline defines Invoke<TStage>(Context) mapping its own
 * stages to method calls). IPipeline itself only carries the stage list.
 *
 *   using FEnginePipeline = IPipeline<IInit, IMain, IShutdown>;
 *   class FWorld : public FLayer<FEnginePipeline> {};
 */
template <typename... TStageTypes>
class MAHO_API IPipeline : public virtual TStageTypes...
{
public:
	virtual ~IPipeline() = default;

	/** Ordered stage-interface list -- the TaskGraph expands one node per stage. */
	using TStages = TTypeList<TStageTypes...>;
};

}