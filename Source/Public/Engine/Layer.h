#pragma once

#include <Core/Interface.h>
#include <Core/TaskGraph.h>
#include <Core/TypeList.h>

#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

/**
 * Layer identity declaration sugar -- generates StaticName() + GetName()
 * override. Usage: class FWorld : public FLayer<...> { MAHO_DECLARE_LAYER(FWorld); ... };
 * The name comes from stringifying the type name (#LayerType); dependency
 * declarations use the same type deduction, so it is self-consistent.
 */
#define MAHO_DECLARE_LAYER(LayerType)                    \
public:                                                  \
	static constexpr std::string_view StaticName()       \
	{                                                    \
		return #LayerType;                                \
	}                                                    \
	std::string_view GetName() const override            \
	{                                                    \
		return StaticName();                              \
	}

namespace Maho
{

// -- 1. FLayer: anonymous layer anchor ----------------------------------------------

/**
 * Anonymous layer anchor - the polymorphic base a (possibly dynamically
 * loaded) feature derives from. It carries identity + per-stage dependency
 * declaration. Lifecycle stages are composed via IPipeline<TStages...>; a
 * layer NEVER manages its deps' lifecycle - the loader/TaskGraph guarantees
 * the execution context is complete before a layer runs. The layer only closes
 * over itself.
 *
 * The layer and its scheduling graph are STRONGLY BOUND: the layer implements
 * an IPipeline (a stage sequence), and FLayerTaskGraph<SamePipeline> drives
 * it. See FLayerTaskGraph below.
 */
class MAHO_API FLayerBase
{
public:
	virtual ~FLayerBase();

	/** Stable identity name -- the TaskGraph topological key. */
	virtual std::string_view GetName() const = 0;

	/** Named dep of `this` at a given stage. */
	struct FDependency
	{
		std::string     Name;    // dep object name
		std::type_index Stage;   // dep object's stage interface (void = unset)
	};

	/** My stage (interface type) -> what I depend on in that stage. */
	using FDependencyTable = std::map<std::type_index, std::vector<FDependency>>;

	virtual const FDependencyTable& GetDependencies() const;

protected:
	FLayerBase() = default;

	/** Declare: `this` at TMyStage depends on TDepObj at TDepStage. */
	template <typename TMyStage, typename TDepObj, typename TDepStage>
	void AddDependency()
	{
		Dependencies[std::type_index(typeid(TMyStage))].push_back({
			std::string(TDepObj::StaticName()),
			std::type_index(typeid(TDepStage))
		});
	}

	/** Runtime dependency: `this` at MyStage depends on DepName at DepStage.
	 *  For dynamically-loaded features that cannot name the dep's type -- the
	 *  dep is addressed by its layer name (== GetName()/StaticName()). */
	void AddDependency(std::type_index MyStage, std::string_view DepName, std::type_index DepStage);

	FDependencyTable Dependencies;
};

/**
 * Layer syntax sugar -- binds FLayerBase (identity + deps) with ONE OR MORE
 * pipelines (ordered stages + the stage-invoke dispatch). Inherit from this
 * instead of spelling the bases:
 *
 *   class FWorld : public FLayer<IPipeline<IMain, IShutdown>>
 *   { ... };
 *
 *   class FWorldMulti : public FLayer<IEngineTickPipeline, IEngineInitPipeline>
 *   { ... };   // multiple pipelines (one layer, several stage sequences)
 *
 * == FLayerBase + TPipelines... (variadic base list).
 * The stage-invoke protocol lives in each IPipeline (see Interface.h).
 */
template <typename... TPipelines>
class MAHO_API FLayer
	: public FLayerBase
	, public TPipelines...
{
};

// -- 2. Empty context placeholder for parameterless pipelines ---------------------------

struct FEmptyContext {};

// -- 3. Extract the stage sequence from a pipeline (pipeline exposes the TStages member) --

template <typename P> struct TStagesOf { using Type = typename P::TStages; };

// -- 4. FLayerTaskGraph: a set of FLayer -> compile -> execute -------------------------

/**
 * Layer task graph -- bridges a set of anonymous FLayer instances into a
 * FTaskGraph. CONTRACT: TPipeline is an IPipeline<TStages...> type; every
 * FLayer passed in MUST implement exactly that pipeline. The layer and the
 * graph declare the SAME pipeline type, pairing them at compile time.
 *
 * Per layer the graph expands one node per stage:
 *   - self-progression: stage N depends on stage N-1 of the SAME layer
 *   - cross-object deps: the layer's own declared deps at that stage
 * Then Compile wires everything; Execute dispatches each ready node through
 * FLayer::Invoke (compile-time stage -> method mapping).
 *
 *   using FPipeline = IPipeline<IMain, IShutdown>;
 *   class FWorld : public FLayer, public FPipeline { ... };
 *   FLayerTaskGraph<FPipeline> G(Pool, { &World, &SSAO });
 *   if (G.Compile()) { G.Execute(); G.Flush(); }
 */

template <typename TPipeline, typename TContext = FEmptyContext>
class FLayerTaskGraph : public FTaskGraph
{
	using FStages = typename TStagesOf<TPipeline>::Type;

public:
	struct FNode : FTaskGraphNode
	{
		FLayerBase* Layer = nullptr;
	};

	FLayerTaskGraph(FThreadPool& InPool, TContext& InContext)
		: FTaskGraph(InPool)
		, Context(InContext)
	{
	}

			/** (Re)build the graph from a layer set -- callable repeatedly (each frame / reconfigure). */
	void Init(std::vector<FLayerBase*> Layers)
	{
		NodeStorage.clear();

		for (FLayerBase* L : Layers)
		{
			if (L == nullptr)
			{
				continue;
			}
			ExpandLayer(L);
		}

		BaseNodes.clear();
		BaseNodes.reserve(NodeStorage.size());
		for (FNode& N : NodeStorage)
		{
			BaseNodes.push_back(&N);
		}
		FTaskGraph::Init(std::move(BaseNodes));
	}

protected:
	void ExecuteNode(FTaskGraphNode* Node) override
	{
		auto* L = static_cast<FNode*>(Node)->Layer;
		Dispatch(L, Node->Stage);
	}

private:

	/** Expand one anonymous layer into one node per stage + edges. */
	void ExpandLayer(FLayerBase* Layer)
	{
		ExpandStagesImpl(Layer, FStages{}, NoStage);
	}

	template <typename TCurrent, typename... TRest>
	void ExpandStagesImpl(FLayerBase* Layer, TTypeList<TCurrent, TRest...>, std::type_index PrevStage)
	{
		FNode Node;
		Node.Name = std::string(Layer->GetName());
		Node.Stage = std::type_index(typeid(TCurrent));
		Node.Layer = Layer;

			// Self-progression: depend on my own previous stage.
			if (PrevStage != NoStage)
			{
				Node.Dependencies.push_back({ Node.Name, PrevStage });
			}

			// Cross-object dependencies: the dependency tuples declared at this stage.
		if (auto It = Layer->GetDependencies().find(Node.Stage);
			It != Layer->GetDependencies().end())
		{
			for (const auto& [DepName, DepStage] : It->second)
			{
				Node.Dependencies.push_back({ DepName, DepStage });
			}
		}

		NodeStorage.push_back(std::move(Node));

		if constexpr (sizeof...(TRest) > 0)
		{
			ExpandStagesImpl<TRest...>(Layer, TTypeList<TRest...>{}, std::type_index(typeid(TCurrent)));
		}
	}

	template <typename... TRest>
	void ExpandStagesImpl(FLayerBase*, TTypeList<>, std::type_index)
	{
	}

			// Runtime stage -> compile-time type match + the layer's embedded Invoke dispatch.
	void Dispatch(FLayerBase* Layer, const std::type_index& Stage)
	{
		DispatchImpl(Layer, Stage, FStages{});
	}

	template <typename TCurrent, typename... TRest>
	void DispatchImpl(FLayerBase* Layer, const std::type_index& Stage, TTypeList<TCurrent, TRest...>)
	{
		if (Stage == std::type_index(typeid(TCurrent)))
		{
				// TPipeline embeds Invoke<TStage> (stage -> method if-constexpr mapping).
				// FLayerBase and TPipeline have no inheritance relation (the layer inherits both); the lateral conversion uses dynamic_cast.
				// A layer that did not mount this pipeline silently skips (in multi-pipeline scenarios, a layer may implement only some of them).
			if (auto* P = dynamic_cast<TPipeline*>(Layer))
			{
				P->template Invoke<TCurrent>(Context);
			}
			return;
		}
		if constexpr (sizeof...(TRest) > 0)
		{
			DispatchImpl<TRest...>(Layer, Stage, TTypeList<TRest...>{});
		}
	}

	template <typename... TRest>
	void DispatchImpl(FLayerBase*, const std::type_index&, TTypeList<>)
	{
	}

	inline static const std::type_index NoStage = std::type_index(typeid(void));

	TContext& Context;
	std::vector<FNode> NodeStorage;
	std::vector<FTaskGraphNode*> BaseNodes;
};

} // namespace Maho
