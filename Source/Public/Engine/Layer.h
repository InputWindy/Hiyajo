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
 * Layer declaration sugar -- generates StaticName() + GetName() + CreateLayer()
 * + GetModulePath(). CreateLayer/GetModulePath are NOT engine-layer-specific:
 * every FLayerBase-derived layer that can be dynamically loaded carries the
 * factory + module path. Usage:
 *
 *   class FWorld : public FLayer<...> { MAHO_DECLARE_LAYER(FWorld, "World.dll"); ... };
 *
 * The name comes from stringifying the type name (#LayerType); dependency
 * declarations use the same type deduction, so it is self-consistent.
 */
#define MAHO_DECLARE_LAYER(LayerType, DLL)               \
public:                                                  \
	static constexpr std::string_view StaticName()       \
	{                                                    \
		return #LayerType;                                \
	}                                                    \
	std::string_view GetName() const override            \
	{                                                    \
		return StaticName();                              \
	}                                                    \
	static Maho::FLayerBase* CreateLayer()               \
	{                                                    \
		return new LayerType();                            \
	}                                                    \
	static std::string_view GetModulePath()              \
	{                                                    \
		return DLL;                                       \
	}

namespace Maho
{

class FEngineBase;
class FLayerBase;

/**
 * Stage dispatch - a free function template specialized per stage interface.
 * The TaskGraph calls Invoke<TStage>(Layer, Engine) at runtime; each stage
 * interface gets a full specialization in Engine.h that casts the layer to the
 * interface and calls its stage method. This replaces the per-pipeline Invoke
 * member: the pipeline class is gone, the stage list is a plain TTypeList.
 */
template <typename TStage>
void Invoke(FLayerBase* Layer, FEngineBase& Engine);

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

} // namespace Maho
