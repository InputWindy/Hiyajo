#pragma once

#include <Core/TaskGraph.h>
#include <Core/ThreadPool.h>
#include <Core/TypeList.h>
#include <Engine/Layer.h>

#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace Maho
{
    
// -- 2. Empty context placeholder for parameterless pipelines ---------------------------

struct FEmptyContext {};

// -- 3. Extract the stage sequence from a pipeline (pipeline exposes the TStages member) --

template <typename P> struct TStagesOf { using Type = typename P::TStages; };

// -- 4. FLayerTaskGraph: a set of FLayer -> compile -> execute -------------------------

/**
 * Layer task graph -- bridges a set of anonymous FLayer instances into a
 * FTaskGraph. CONTRACT: TStages is a TTypeList<StageInterface...>; every
 * FLayer passed in MUST implement every stage interface it mounts (filtered by
 * the caller via FQuery). Per layer the graph expands one node per stage:
 *   - self-progression: stage N depends on stage N-1 of the SAME layer
 *   - cross-object deps: the layer's own declared deps at that stage
 * Then Compile wires everything; Execute dispatches each ready node through the
 * free function Invoke<TStage>(Layer, Engine).
 *
 *   using FTickStages = TTypeList<IBeginFrame, ITick, IEndFrame, IExit>;
 *   FLayerTaskGraph<FTickStages> G(Pool, Engine);
 *   G.Init(Engine.Select<IBeginFrame, ITick, IEndFrame, IExit>());  // or FQuery
 *   if (G.Compile()) { G.Execute(); G.Flush(); }
 */

template <typename TStages, typename TContext = FEmptyContext>
class FLayerTaskGraph : public FTaskGraph
{
	using FStages = TStages;

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

		// Apply reverse dependencies (BlockOn): the declaring layer is the CONSUMER
		// that knows the producer; it adds an edge "OtherName@Stage -> my MyStage" to
		// the producer's node. Skipped when the target is not in this graph (the
		// producer is not installed) -- a consumer's declaration degrades gracefully.
		for (FLayerBase* L : Layers)
		{
			if (L == nullptr)
			{
				continue;
			}
			for (const auto& Dep : L->GetDependents())
			{
				for (FNode& N : NodeStorage)
				{
					if (N.Name == Dep.Name && N.Stage == Dep.Stage)
					{
						N.Dependencies.push_back({ std::string(L->GetName()), Dep.MyStage });
						break;
					}
				}
			}
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
			// Free function dispatch: Invoke<TStage, TContext>(Layer, Context) is
			// specialized per (stage, context) pair. A layer that does not implement
			// this stage interface silently skips (dynamic_cast inside the specialization).
			Invoke<TCurrent, TContext>(Layer, Context);
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

}