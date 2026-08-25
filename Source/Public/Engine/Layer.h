#pragma once

#include <Core/Assembly.h>
#include <Core/Extension.h>
#include <Core/Queue.h>
#include <Core/Singleton.h>
#include <Core/Topology.h>
#include <Core/Schedulers.h>

#include <functional>
#include <string_view>
#include <utility>
#include <vector>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// The layer vocabulary — capabilities, the IPlugin composer, the module
// contract, and the FLayer node. Every layer instance is a FLayer<...> (derived
// from the FLayerBase root anchor in Core/Interface.h).
// ───────────────────────────────────────────────────────────────────────

/** Main capability — the layer's free-form run entry (a layer owns its loop). */
class MAHO_API IMain
{
public:
	virtual ~IMain() = default;
	virtual int Main() = 0;
};

/** Exit capability — request a running loop (IMain) to stop. */
class MAHO_API IExit
{
public:
	virtual ~IExit() = default;
	virtual void Exit() = 0;
};

/**
 * Capability composer — a virtual base that installs every capability trait as
 * a virtual base, so a contract = a list of stages/interfaces it promises.
 *
 *   class FRenderer : public FLayer<...>, public virtual IPlugin<IMain, IRenderer> {};
 */
template <typename... TCapabilities>
class MAHO_API IPlugin : public virtual TCapabilities...
{
public:
	virtual ~IPlugin() = default;
};

/**
 * Module-instance contract — a concrete layer type knows which DLL to load it
 * from. FLayer::Load maps T → T::GetModulePath() armed with FModuleInstance.
 */
template <typename T>
concept FModuleInstance = requires
{
	{ T::GetModulePath() } -> std::convertible_to<std::string_view>;
};

template <typename... FChildrenTypes>
class FLayer;

// ───────────────────────────────────────────────────────────────────────
// Layer root anchor + install/uninstall command.
// ───────────────────────────────────────────────────────────────────────

class FLayerBase;

/**
 * A deferred install/uninstall intent over a target layer.
 *
 * A value command: an (Op, Target) pair — "install layer X" or "uninstall layer
 * X". The consumer (a layer holding an FQueue) Enqueues these at
 * any time; Flush() at a safe point runs each one, which calls the target's
 * Initialize / Shutdown. Dedupe key is (Op, Target), so a repeated request for
 * the same operation on the same layer queues once.
 *
 * bStatic distinguishes the two schedules: a STATIC target's type is declared in
 * the layer's FChildren (so it joins the compile-time topo levels); a dynamic
 * target is a runtime-installed DLL layer assumed independent of everything.
 */
struct FLayerCommand : public ICommand
{
	enum class EOp
	{
		Install,
		Uninstall,
		Callback,
	};

	EOp Op = EOp::Install;
	FLayerBase* Target = nullptr;
	bool bStatic = false;
	std::function<void()> Action;   // Callback 命令的负载（Flush 时执行）

	FLayerCommand() = default;
	FLayerCommand(EOp InOp, FLayerBase* InTarget, bool InStatic = false)
		: Op(InOp)
		, Target(InTarget)
		, bStatic(InStatic)
	{
	}
	FLayerCommand(EOp InOp, std::function<void()> InAction)
		: Op(InOp)
		, Action(std::move(InAction))
	{
	}

	/** The generic multi-threaded enqueue factory — FQueue::EnqueueCommand uses this. */
	static FLayerCommand Callback(std::function<void()> Fn)
	{
		return FLayerCommand(EOp::Callback, std::move(Fn));
	}

	bool operator==(const FLayerCommand& O) const
	{
		// Callback 命令有状态、不可比 → 不去重；Install/Uninstall 按 (Op,Target) 去重。
		if (Op == EOp::Callback)
		{
			return false;
		}
		return Op == O.Op && Target == O.Target;
	}

	void Execute() override;
};

/**
 * Layer root anchor — the base of every layer instance, and a command consumer.
 *
 * Inherits FQueue, so a layer owns the deferred install/uninstall
 * intents of its children: Enqueue a command from any thread, Flush at a safe
 * point (each command runs the target's Initialize / Shutdown). No explicit
 * init/shutdown stages exist — install IS the init, uninstall IS the shutdown,
 * both command-driven.
 *
 * It is the single non-template polymorphic node that containers and runtime
 * type dispatch build on (see Engine/Schedulers.h). Every root is MAHO_API (one
 * vtable / RTTI shared across the DLL boundary) so host-side dynamic_cast keeps
 * a single ABI.
 */
class MAHO_API FLayerBase
	: public FQueue
	, public Parallel::FParallelScheduler
{
public:
	virtual ~FLayerBase() = default;

	/** Type-agnostic query over a type table — FLayerBase composes Core::Query. */
	template <typename FList>
	[[nodiscard]] constexpr auto Query() const
	{
		return TQuery<FList>{};
	}

	/** True when this instance derives from T (a runtime LINQ filter helper). */
	template <typename T>
	bool Is() const
	{
		return dynamic_cast<const T*>(this) != nullptr;
	}

	/**
	 * Called when this layer is initialized (install IS the init). Receives the
	 * launch arguments so a layer can start up with command-line config.
	 */
	virtual void Initialize(int Argc, char** Argv) {}

	/** Called when this layer is shut down (uninstall IS the shutdown). */
	virtual void Shutdown() {}

	/**
	 * Drive SINGLETON types level-by-level (their compile-time topo on
	 * FDefaultSlot): each T's T::Get() singleton is handed to visitor as T&.
	 * Levels serialized, singletons within a level run in parallel. The singleton
	 * type table is given; no runtime instance array involved.
	 *
	 *   Layer->ForEachSingleton<FLog, FNet>([](auto& S) { S.Tick(); });
	 */
	template <typename... FTypes, typename TVisitor>
	void ForEachSingleton(TVisitor&& Visitor) const
	{
		ForEachSingletonList<TTypeList<FTypes...>>(std::forward<TVisitor>(Visitor));
	}

	/// Variant taking a TTypeList directly (e.g. a Core::Query::FResult).
	template <typename FList, typename TVisitor>
	void ForEachSingletonList(TVisitor&& Visitor) const
	{
		using FLevels = Topo::TLevels_t<FList, FDefaultSlot>;
		Parallel::FParallelScheduler Sched;
		Maho::ForEach<FLevels>(Maho::FSerialTraversePolicy{}, [&](auto Tag) {
			using FLevel = typename decltype(Tag)::Type;
			std::vector<std::function<void()>> Tasks;
			Tasks.reserve(FLevel::Count);
			Maho::ForEach<FLevel>(Maho::FSerialTraversePolicy{}, [&](auto TypeTag) {
				using T = typename decltype(TypeTag)::Type;
				Tasks.emplace_back([&] { Visitor(T::Get()); });
			});
			Sched.ForEach(Tasks, [](const std::function<void()>& T) { return T; });
		});
	}
};

// ───────────────────────────────────────────────────────────────────────
// Runtime instance dispatch — FLayer-scoped (FLayerBase*, the polymorphic layer
// node). At compile time a TList of candidate types is tried by dynamic_cast;
// the first match sees Visitor(T&). Each instance is driven at most once;
// instances matching no candidate are skipped. Distinct from the type-agnostic
// Core/Query.h filtering (which never touches runtime instances).
// ───────────────────────────────────────────────────────────────────────
namespace InstanceDispatchDetail
{
	template <typename THead, typename... TRest, typename TVisitor>
	bool TCall(FLayerBase* Instance, TVisitor& Visitor)
	{
		if (auto* Typed = dynamic_cast<THead*>(Instance))
		{
			Visitor(*Typed);
			return true;
		}
		if constexpr (sizeof...(TRest) > 0)
		{
			return TCall<TRest...>(Instance, Visitor);
		}
		return false;
	}
}

/** Drive Instance once: first matching type in TList sees Visitor(T&). */
template <typename TList, typename TVisitor>
void DispatchInstance(FLayerBase* Instance, TVisitor& Visitor);

template <typename... Ts, typename TVisitor>
void DispatchInstance(TTypeList<Ts...>, FLayerBase* Instance, TVisitor& Visitor)
{
	if (Instance == nullptr)
	{
		return;
	}
	InstanceDispatchDetail::TCall<Ts...>(Instance, Visitor);
}

/** Drive Instance once: first matching type in TList sees Visitor(T&). */
template <typename TList, typename TVisitor>
void DispatchInstance(FLayerBase* Instance, TVisitor& Visitor)
{
	DispatchInstance(TList{}, Instance, Visitor);
}

inline void FLayerCommand::Execute()
{
	if (Op == EOp::Callback)
	{
		if (Action)
		{
			Action();
		}
		return;
	}
	if (!Target)
	{
		return;
	}
	if (Op == EOp::Install)
	{
		Target->Initialize(0, nullptr); // args come at bootstrap; a mid-run install has none
	}
	else
	{
		Target->Shutdown();
	}
}

/**
 * Runtime LINQ over a layer's LIVE Instances. Select<T>() keeps every instance
 * that implements T (a runtime dynamic_cast over Instances — NOT the compile-time
 * type algebra), then ForEach drives the subset level-by-level in parallel.
 *
 *   Query().Select<IRender>().ForEach([](IRender& R) { R.Render(); });
 *
 * TSelected accumulates the interfaces to match (OR — an instance passes if it
 * implements any selected interface).
 */
template <typename FLayerType, typename... TSelected>
class FLayerQuery
{
public:
	FLayerQuery() = default;

	/** Bind to the owning layer (FLayer inherits this and binds itself). */
	void SetLayer(FLayerType* InLayer)
	{
		Layer = InLayer;
	}

	/** Keep instances implementing every TInterfaces... (runtime filter, OR'd). */
	template <typename... TInterfaces>
	[[nodiscard]] auto Select() const
	{
		return FLayerQuery<FLayerType, TSelected..., TInterfaces...>{ *Layer };
	}

		/**
		 * Drive the selected subset level-by-level (FLevels): levels serialized
		 * (barrier after each), instances within a level parallel. Each static
		 * instance is driven in its topo level; dynamic instances (mutually
		 * independent) run in one parallel batch after the levels.
		 *
		 *   Query().Select<IRender>().ForEach([](IRender& R) { R.Render(); });
		 */
			template <typename TVisitor>
			void ForEach(TVisitor&& Visitor) const
			{
				static_assert(sizeof...(TSelected) > 0,
					"Query().ForEach requires Select<T>() first");
				using FList = TTypeList<TSelected...>;

				// ① singleton segment — compile-time types in FLayers that are
				// CRTP singletons AND implement a selected interface: drive T::Get().
				using FLevels = typename FLayerType::FLevels;
				Maho::ForEach<FLevels>(Maho::FSerialTraversePolicy{}, [&](auto Tag) {
					using FLevel = typename decltype(Tag)::Type;
					std::vector<std::function<void()>> Tasks;
					Maho::ForEach<FLevel>(Maho::FSerialTraversePolicy{}, [&](auto TypeTag) {
						using T = typename decltype(TypeTag)::Type;
						EmplaceIf<T>(Tasks, Visitor);
					});
					Layer->Parallel::FParallelScheduler::ForEach(Tasks, [](const std::function<void()>& T) { return T; });
				});

				// collect the selected subset once up front (safe snapshot)
				std::vector<FLayerBase*> StaticSubset;
				for (FLayerBase* I : Layer->Instances)
				{
					if ((I->template Is<TSelected>() || ...))
					{
						StaticSubset.push_back(I);
					}
				}
				std::vector<FLayerBase*> DynSubset;
				for (FLayerBase* I : Layer->DynamicLayers)
				{
					if ((I->template Is<TSelected>() || ...))
					{
						DynSubset.push_back(I);
					}
				}

				// static: topological levels (barrier between, parallel within)
				Maho::ForEach<FLevels>(Maho::FSerialTraversePolicy{}, [&](auto Tag) {
					using FLevel = typename decltype(Tag)::Type;
					std::vector<std::function<void()>> Tasks;
					Tasks.reserve(StaticSubset.size());
					for (FLayerBase* I : StaticSubset)
					{
						Tasks.emplace_back([&, I] {
							bool bOwned = false;
							auto InLevel = [bOwned = &bOwned](auto&) { *bOwned = true; };
							DispatchInstance<FLevel>(I, InLevel);
							if (bOwned)
							{
								DispatchInstance<FList>(I, Visitor);
							}
						});
					}
					Layer->Parallel::FParallelScheduler::ForEach(Tasks, [](const std::function<void()>& T) { return T; });
				});

				// dynamic: mutually independent — one parallel batch, no ordering
				if (!DynSubset.empty())
				{
					std::vector<std::function<void()>> Tasks;
					Tasks.reserve(DynSubset.size());
					for (FLayerBase* I : DynSubset)
					{
						Tasks.emplace_back([&, I] { DispatchInstance<FList>(I, Visitor); });
					}
					Layer->Parallel::FParallelScheduler::ForEach(Tasks, [](const std::function<void()>& T) { return T; });
				}
			}

			/** Direct reference binder used by Select's return (and the layer base). */
			explicit FLayerQuery(FLayerType& InLayer)
				: Layer(&InLayer)
			{
			}

			/** Singleton-or-not tag dispatch — avoids MSVC `if constexpr` in lambda. */
			template <typename T, typename TVisitor>
			static void EmplaceIf(std::vector<std::function<void()>>& Tasks, TVisitor& Visitor)
			{
				if constexpr (std::is_base_of_v<TSingleton<T>, T>
					&& (std::is_base_of_v<TSelected, T> || ...))
				{
					Tasks.emplace_back([&] { Visitor(T::Get()); });
				}
			}

private:
			FLayerType* Layer = nullptr;
};

/**
 * Layer base — a NODE: it owns child instances (vector<FLayerBase*>), the
 * parallel scheduler to drive them, and the module loading to get them. Users
 * interact ONLY through the Query / ForEach lambdas — modules, DLLs, the base
 * anchor are invisible.
 *
 *   struct FGame : FLayer<FRenderer, FNetWork, FGameWorld>, IPlugin<IRender>
 *   {
 *       void Render() override {}
 *       int Main() override
 *       {
 *           Query().Select<IRender>().ForEach([](IRender& R) { R.Render(); });
 *           return 0;
 *       }
 *   };
 *
 * Children are spelled flat (plain types, not a TTypeList) — use as few or as
 * many as a layer owns. The dependency table is separate (MAHO_EXTEND_DEPS →
 * Topo::TLevels_t): FLayer<...> lists WHO this layer drives, MAHO_EXTEND_DEPS
 * lists WHO this layer depends on for ordering. Unrelated axes.
 *
 * Lifecycle defaults: Initialize loads every child type (FModuleInstance
 * contract), Shutdown destroys what was loaded and unloads the modules.
 * Instances is public so a host may inject its own; only FLayer-loaded
 * instances are deleted. IMain is NOT composed — a layer declares its own
 * drive interface via IPlugin<IMain, ...> when it runs per frame. GetModulePath
 * lives on each derived layer; the base cannot know which DLL a layer ships in.
 */
template <typename... FChildrenTypes>
class FLayer
	: public FLayerBase
	, public FLayerQuery<FLayer<FChildrenTypes...>>
	, public Parallel::FParallelScheduler
{
public:
	/** The child type table — the compile-time scan list this layer drives. */
	using FLayers = TTypeList<FChildrenTypes...>;
	using FChildren = FLayers;
	using FScheduler = Parallel::FParallelScheduler;

	/**
	 * Topology: children leveled by their MAHO_EXTEND_DEPS declarations on the
	 * FDefaultSlot key. Level 0 runs first (no dependencies), each later level
	 * after its deps. Inner level types may run in parallel; levels are barriers.
	 */
	using FLevels = Topo::TLevels_t<FChildren, FDefaultSlot>;

private:
	using FQueryBase = FLayerQuery<FLayer<FChildrenTypes...>>;

public:
	FLayer()
	{
		// the layer IS its own query — bind it so Query().Select<>().ForEach works
		FQueryBase::SetLayer(this);
	}

	~FLayer() override
	{
		// tear down every owned child (static + dynamic) via the command path
		for (FLayerBase* C : Instances)
		{
			Enqueue(FLayerCommand{ FLayerCommand::EOp::Uninstall, C });
		}
		for (FLayerBase* C : DynamicLayers)
		{
			Enqueue(FLayerCommand{ FLayerCommand::EOp::Uninstall, C });
		}
		ProcessCommands();
		Instances.clear();
		DynamicLayers.clear();
	}

	// ── dynamic install / uninstall (pending commands, applied at Flush) ──

	/**
	 * Queue a STATIC child of type T (declared in this layer's FChildren — it
	 * joins the compile-time topo levels) to be installed at the next Flush.
	 */
	template <typename T> requires FModuleInstance<T>
	void Install()
	{
		if (T* Child = Load<T>())
		{
			Enqueue(FLayerCommand{ FLayerCommand::EOp::Install, Child, /*static*/ true });
		}
	}

	/**
	 * Queue a DYNAMIC layer (loaded from a DLL at runtime, its type not declared
	 * in FChildren) to be installed at the next Flush. Dynamic layers are assumed
	 * mutually independent — they do NOT join the topo levels.
	 */
	void Install(std::string_view DLLPath)
	{
		if (FLayerBase* Child = Load(DLLPath))
		{
			Enqueue(FLayerCommand{ FLayerCommand::EOp::Install, Child, /*dynamic*/ false });
		}
	}

	/**
	 * Queue a child to be removed (and destroyed) at the next Flush. The child
	 * stays live until then. Locates by pointer — no compile-time type needed.
	 */
	void Uninstall(FLayerBase* Child)
	{
		Enqueue(FLayerCommand{ FLayerCommand::EOp::Uninstall, Child });
	}

	/** Queue the removal of every installed child (static + dynamic). */
	void UninstallAll()
	{
		for (FLayerBase* C : Instances)
		{
			Enqueue(FLayerCommand{ FLayerCommand::EOp::Uninstall, C });
		}
		for (FLayerBase* C : DynamicLayers)
		{
			Enqueue(FLayerCommand{ FLayerCommand::EOp::Uninstall, C });
		}
	}

	/**
	 * Apply pending install/uninstall commands — call between frames (a safe
	 * point). Installs Initialize() + run, uninstalls Shutdown() + detach.
	 */
	void Flush()
	{
		ProcessCommands();
	}

	// ── drive: LINQ Query over live Instances ──

	// ── drive: the layer IS its query — Select<...>().ForEach directly ──

	/**
	 * Drive every instance whose runtime type matches TList, handing each to the
	 * visitor as that type:
	 *
	 *   ForEach(TTypeList<FWindow, FScene>{}, [](auto& L) { ... });
	 */
	template <typename TList, typename TVisitor>
	void ForEach(TList, TVisitor&& Visitor)
	{
		for (FLayerBase* I : Instances)
		{
			DispatchInstance<TList>(I, Visitor);
		}
	}

	/** Return the first installed child of type T (static or dynamic); nullptr if none. */
	template <typename T>
	T* FindChild()
	{
		for (FLayerBase* I : Instances)
		{
			if (auto* Typed = dynamic_cast<T*>(I))
			{
				return Typed;
			}
		}
		for (FLayerBase* I : DynamicLayers)
		{
			if (auto* Typed = dynamic_cast<T*>(I))
			{
				return Typed;
			}
		}
		return nullptr;
	}

private:
	/** The runtime LINQ query drives Instances — grant it access. */
	template <typename FLayerT, typename... TSelectedT>
	friend class FLayerQuery;

	/** STATIC children — declared in FChildren, join the compile-time topo levels. */
	std::vector<FLayerBase*> Instances;

	/** DYNAMIC children — runtime-installed DLL layers, assumed mutually independent. */
	std::vector<FLayerBase*> DynamicLayers;

	/** Drain the inherited command queue and apply each install/uninstall. */
		void ProcessCommands()
		{
			// drain the FIFO one at a time, then apply each command
			while (auto Cmd = DequeueOne())
			{
				if (Cmd->Op == FLayerCommand::EOp::Install)
				{
					if (Cmd->Target)
					{
						(Cmd->bStatic ? Instances : DynamicLayers).push_back(Cmd->Target);
						Cmd->Target->Initialize(0, nullptr); // mid-run install: no launch args
					}
				}
				else
				{
					if (Cmd->Target)
					{
						Cmd->Target->Shutdown();
						Erase(Instances, Cmd->Target);
						Erase(DynamicLayers, Cmd->Target);
						delete Cmd->Target; // owned child — free it
					}
				}
			}
		}

	/**
	 * Load a STATIC child of type T — the module path comes from T::GetModulePath()
	 * (for CoreTest no real DLLs; a fresh default instance is returned).
	 */
	template <typename T> requires FModuleInstance<T>
	T* Load()
	{
		return new T(); // (real DLL loading via FAssembly when modules exist)
	}

	/**
	 * Load a module from DLLPath and construct its root instance (the DLL's
	 * CreateExtension symbol); nullptr on failure or a missing factory.
	 */
	FLayerBase* Load(std::string_view DLLPath)
	{
		auto& M = LoadedModules.emplace_back();
		if (M.Assembly.Load(DLLPath))
		{
			using CreateFunction = FLayerBase* (*)();
			auto Create = M.Assembly.GetProcAs<CreateFunction>("CreateExtension");
			if (Create)
			{
				return Create();
			}
		}
		LoadedModules.pop_back();
		return nullptr;
	}

	struct FLoadedModule
	{
		FAssembly Assembly;
	};
	std::vector<FLoadedModule> LoadedModules;

	static void Erase(std::vector<FLayerBase*>& V, FLayerBase* P)
	{
		for (auto it = V.begin(); it != V.end(); ++it)
		{
			if (*it == P)
			{
				V.erase(it);
				return;
			}
		}
	}

};

} // namespace Maho

// A blank FLayer-derived scaffold — match Base + interfaces + inline DLL factory.
//
//   class FMyPlugin
//       : public Maho::FLayer<>
//       , public Maho::IPlugin<IMyInterface>   // hand-written interfaces
//   {
//       MAHO_DECLARE_LAYER(FMyPlugin, "MyPlugin.dll");
//   };
//
// Expands inside a class body to an inline CreateExtension (the DLL factory)
// + GetModulePath — pure boilerplate the user never edits. Lifecycle hooks
// (Initialize/Shutdown) stay VISIBLE in the class, since the user usually
// writes logic there. Interfaces stay a hand-written template arg — no code-gen.
#define MAHO_DECLARE_LAYER(CustomBase, AppDLL)          \
public:                                                 \
	static Maho::FLayerBase* CreateExtension()          \
	{                                                   \
		return new CustomBase();                        \
	}                                                   \
	static std::string_view GetModulePath()             \
	{                                                   \
		return AppDLL;                                  \
	}                                                   \

