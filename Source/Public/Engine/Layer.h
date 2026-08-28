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
 * Layer 身份声明语法糖 —— 生成 StaticName() + GetName() 覆盖。
 * 用法：class FWorld : public FLayer<...> { MAHO_DECLARE_LAYER(FWorld); ... };
 * 名字取自类型名字符串化（#LayerType），依赖声明用同一类型推导，自洽。
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

// ── ① FLayer：匿名层锚点 ──────────────────────────────────────────────────

/**
 * Anonymous layer anchor — the polymorphic base a (possibly dynamically
 * loaded) feature derives from. It carries identity + per-stage dependency
 * declaration. Lifecycle stages are composed via IPipeline<TStages...>; a
 * layer NEVER manages its deps' lifecycle — the loader/TaskGraph guarantees
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
	virtual ~FLayerBase() = default;

	/** Stable identity name — the TaskGraph topological key. */
	virtual std::string_view GetName() const = 0;

	/** Named dep of `this` at a given stage. */
	struct FDependency
	{
		std::string     Name;    // dep object name
		std::type_index Stage;   // dep object's stage interface (void = unset)
	};

	/** My stage (interface type) → what I depend on in that stage. */
	using FDependencyTable = std::map<std::type_index, std::vector<FDependency>>;

	virtual const FDependencyTable& GetDependencies() const
	{
		return Dependencies;
	}

protected:
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
	 *  For dynamically-loaded features that cannot name the dep's type — the
	 *  dep is addressed by its layer name (== GetName()/StaticName()). */
	void AddDependency(std::type_index MyStage, std::string_view DepName, std::type_index DepStage)
	{
		Dependencies[MyStage].push_back({ std::string(DepName), DepStage });
	}

	FDependencyTable Dependencies;
};

/**
 * Layer syntax sugar — binds FLayerBase (identity + deps) with an IPipeline
 * (ordered stages + the stage-invoke dispatch). Inherit from this instead of
 * spelling both bases:
 *
 *   class FWorld : public FLayer<IPipeline<IMain, IShutdown>>
 *   { ... };
 *
 * == FLayerBase + IPipeline<IMain, IShutdown>.
 * The stage-invoke protocol lives in IPipeline (see Interface.h).
 */
template <typename TPipeline>
class MAHO_API FLayer
	: public FLayerBase
	, public TPipeline
{
};

// ── ② 无参管线的空上下文占位 ─────────────────────────────────────────────

struct FEmptyContext {};

// ── ③ 从 pipeline 提取 stage 序列（pipeline 暴露 TStages 成员）─────────

template <typename P> struct TStagesOf { using Type = typename P::TStages; };

// ── ④ FLayerTaskGraph：一组 FLayer → 编译 → 执行 ─────────────────────────

/**
 * Layer task graph — bridges a set of anonymous FLayer instances into a
 * FTaskGraph. CONTRACT: TPipeline is an IPipeline<TStages...> type; every
 * FLayer passed in MUST implement exactly that pipeline. The layer and the
 * graph declare the SAME pipeline type, pairing them at compile time.
 *
 * Per layer the graph expands one node per stage:
 *   - self-progression: stage N depends on stage N-1 of the SAME layer
 *   - cross-object deps: the layer's own declared deps at that stage
 * Then Compile wires everything; Execute dispatches each ready node through
 * FLayer::Invoke (compile-time stage → method mapping).
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

	/** (Re)build the graph from a layer set — callable repeatedly (每帧/重配). */
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

		// 自递进：依赖自己前一个 stage。
		if (PrevStage != NoStage)
		{
			Node.Dependencies.push_back({ Node.Name, PrevStage });
		}

		// 跨对象依赖：本 stage 声明的依赖元组。
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

	// 运行时 stage → 编译期类型匹配 + 层内嵌 Invoke 分发。
	void Dispatch(FLayerBase* Layer, const std::type_index& Stage)
	{
		DispatchImpl(Layer, Stage, FStages{});
	}

	template <typename TCurrent, typename... TRest>
	void DispatchImpl(FLayerBase* Layer, const std::type_index& Stage, TTypeList<TCurrent, TRest...>)
	{
		if (Stage == std::type_index(typeid(TCurrent)))
		{
			// TPipeline 内嵌 Invoke<TStage>（stage → 方法的 if-constexpr 映射）。
			// FLayerBase 与 TPipeline 无继承关系（层同时继承两者），侧向转换用 dynamic_cast。
			dynamic_cast<TPipeline&>(*Layer).template Invoke<TCurrent>(Context);
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
