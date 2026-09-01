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

class FLayerBase;

/**
 * Stage dispatch - a free function template specialized per (stage, context)
 * pair. FLayerTaskGraph calls Invoke<TStage>(Layer, Context) at runtime; each
 * stage interface gets full specializations per context type (e.g.
 * Invoke<IInit, FEngineBase> in Engine.h, Invoke<IBeginRender, FRender> in
 * Render.h). A layer that does not implement the interface silently skips.
 */
template <typename TStage, typename TContext>
void Invoke(FLayerBase* Layer, TContext& Context);

/**
 * Stage dispatch specialization sugar - full-specializes Invoke<TStage,
 * TContext> to dynamic_cast the layer to CastType and call Method(Context).
 * Each context type (FEngineBase, FRender, ...) declares its own specializations
 * for the stage interfaces it schedules.
 *
 *   MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IInit, IInit, Initialize)
 *   // => Invoke<IInit, FEngineBase>(Layer, Engine) -> cast IInit -> Initialize(Engine)
 */
#define MAHO_DECLARE_STAGE_DISPATCH(ContextType, StageType, CastType, Method) \
template <>                                                                    \
inline void Invoke<StageType, ContextType>(FLayerBase* Layer, ContextType& Context) \
{                                                                              \
	if (auto* S = dynamic_cast<CastType*>(Layer))                              \
	{                                                                          \
		S->Method(Context);                                                     \
	}                                                                          \
}

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

	/** Reverse dependency: "OtherName at Stage waits for MY MyStage" -- the edge
	 *  a CONSUMER declares on behalf of a producer that does not know it is used
	 *  (e.g. "the Log layer must outlive my teardown"). Applied by the graph at
	 *  Init; skipped when OtherName is not installed. */
	struct FDependent
	{
		std::string     Name;      // layer that must wait for me
		std::type_index Stage;     // its stage that waits
		std::type_index MyStage;   // my stage it is blocked on
	};
	const std::vector<FDependent>& GetDependents() const { return Dependents; }

protected:
	FLayerBase() = default;

	// -- forward dependency: MY TMyStage waits for TOther@TOtherStage ----------
	/** Typed: the consumer names the producer's type (it uses it, so it knows it). */
	template <typename TMyStage, typename TOther, typename TOtherStage>
	void WaitFor()
	{
		Dependencies[std::type_index(typeid(TMyStage))].push_back({
			std::string(TOther::StaticName()),
			std::type_index(typeid(TOtherStage))
		});
	}

	/** Anonymous: same edge addressed by the producer's layer name, for
	 *  dynamically-loaded plugins that reference each other without a compile-time
	 *  type coupling (no header include). */
	void WaitFor(std::type_index MyStage, std::string_view OtherName, std::type_index OtherStage);

	// -- reverse dependency: TOther@TOtherStage waits for MY TMyStage ----------
	/** Typed: "TOther is blocked on me at TMyStage" (TOther runs after me). */
	template <typename TOther, typename TOtherStage, typename TMyStage>
	void BlockOn()
	{
		Dependents.push_back({
			std::string(TOther::StaticName()),
			std::type_index(typeid(TOtherStage)),
			std::type_index(typeid(TMyStage))
		});
	}

	/** Anonymous: same edge addressed by the other layer's name. */
	void BlockOn(std::string_view OtherName, std::type_index OtherStage, std::type_index MyStage);

	FDependencyTable Dependencies;
	std::vector<FDependent> Dependents;
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
