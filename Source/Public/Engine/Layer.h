#pragma once

#include <Core/Assembly.h>
#include <Core/Extension.h>
#include <Core/Interface.h>
#include <Core/Queue.h>
#include <Core/Query.h>
#include <Core/Singleton.h>
#include <Core/Topology.h>
#include <Core/Schedulers.h>

#include <algorithm>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

namespace Maho
{

template <typename T>
concept FModuleInstance = requires
{
	{ T::GetModulePath() } -> std::convertible_to<std::string_view>;
};

// ── layer command catalog (queue lanes) ──────────────────────────────────
class FLayerBase;

enum class ELayerCommand : std::uint64_t
{
	Install = 1,
	Uninstall = 2,
};

/** Install command — carries a child instance to be adopted at FlushCommands. */
struct FInstallCommand : public ICommand
{
	FLayerBase* Child = nullptr;

	[[nodiscard]] std::uint64_t GetCatalogId() const override
	{
		return static_cast<std::uint64_t>(ELayerCommand::Install);
	}
};

/** Uninstall command — carries a child instance to be released at FlushCommands. */
struct FUninstallCommand : public ICommand
{
	FLayerBase* Child = nullptr;

	[[nodiscard]] std::uint64_t GetCatalogId() const override
	{
		return static_cast<std::uint64_t>(ELayerCommand::Uninstall);
	}
};

class MAHO_API FLayerBase
	: public FQueue
	, public Parallel::FParallelScheduler
{
public:
	virtual ~FLayerBase() = default;

	/** True when this instance derives from T (a runtime LINQ filter helper). */
	template <typename T>
	bool Is() const
	{
		return dynamic_cast<const T*>(this) != nullptr;
	}
};

// ── compile-time "is a TSingleton" / "level table is all singletons" ──────
namespace LayerDetail
{
	template <typename T>
	struct TIsSingleton : std::is_base_of<TSingleton<T>, T>
	{
	};

	template <typename FLevel>
	struct TLevelAllSingleton;
	template <>
	struct TLevelAllSingleton<TTypeList<>> : std::true_type
	{
	};
	template <typename THead, typename... TRest>
	struct TLevelAllSingleton<TTypeList<THead, TRest...>>
		: std::bool_constant<
			TIsSingleton<THead>::value
			&& TLevelAllSingleton<TTypeList<TRest...>>::value>
	{
	};

	template <typename FLevels>
	struct TAllSingleton;
	template <>
	struct TAllSingleton<TTypeList<>> : std::true_type
	{
	};
	template <typename FLevel, typename... FRest>
	struct TAllSingleton<TTypeList<FLevel, FRest...>>
		: std::bool_constant<
			TLevelAllSingleton<FLevel>::value
			&& TAllSingleton<TTypeList<FRest...>>::value>
	{
	};
}

// ── runtime instance dispatch over FLayerBase (first matching type wins) ──
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

template <typename TList, typename TVisitor>
void DispatchInstance(FLayerBase* Instance, TVisitor& Visitor)
{
	DispatchInstance(TList{}, Instance, Visitor);
}

template <typename... TRegisteredTypes>
class FLayer
	: public FLayerBase
{
public:
	using FLayers = TTypeList<TRegisteredTypes...>;
	using FScheduler = Parallel::FParallelScheduler;

public:
	FLayer()
	{
		Maho::ForEach<FLayers>(Maho::FSerialTraversePolicy{}, [this](auto TypeTag)
		{
			using T = typename decltype(TypeTag)::Type;
			if (auto* Child = Load(T::GetModulePath()))
			{
				auto Cmd = std::make_unique<FInstallCommand>();
				Cmd->Child = Child;
				Enqueue(std::move(Cmd));
			}
		});
		FlushCommands();
	}

	~FLayer() override
	{
		for (FLayerBase* L : Layers)
		{
			delete L;
		}
		Layers.clear();
	}

	template <typename T> requires FModuleInstance<T>
	void Install()
	{
		if (auto* Child = Load(T::GetModulePath()))
		{
			auto Cmd = std::make_unique<FInstallCommand>();
			Cmd->Child = Child;
			Enqueue(std::move(Cmd));
		}
	}

	void Uninstall(FLayerBase* Child)
	{
		auto Cmd = std::make_unique<FUninstallCommand>();
		Cmd->Child = Child;
		Enqueue(std::move(Cmd));
	}

	/**
	 * Apply pending install/uninstall commands at a safe point. Commands were
	 * Enqueued from any thread; this drains them FIFO per catalog lane:
	 *   Install   → adopt the child into Layers
	 *   Uninstall → release the child (delete + erase from Layers)
	 */
	virtual void FlushCommands()
	{
		while (auto Cmd = FQueue::Dequeue(static_cast<std::uint64_t>(ELayerCommand::Install)))
		{
			auto* Inst = dynamic_cast<FInstallCommand*>(Cmd.get());
			if (Inst && Inst->Child)
			{
				Layers.push_back(Inst->Child);
			}
		}
		while (auto Cmd = FQueue::Dequeue(static_cast<std::uint64_t>(ELayerCommand::Uninstall)))
		{
			auto* Un = dynamic_cast<FUninstallCommand*>(Cmd.get());
			if (Un && Un->Child)
			{
				auto It = std::find(Layers.begin(), Layers.end(), Un->Child);
				if (It != Layers.end())
				{
					Layers.erase(It);
				}
				delete Un->Child;
			}
		}
	}

	/** Type-agnostic query over a type table — FLayerBase composes Core::Query. */
	[[nodiscard]] constexpr auto Query() const
	{
		return TQuery<FLayers>{};
	}

	/**
	 * Drive the installed instances level-by-level. FLevels is the compile-time
	 * level table (e.g. Topo::TLevels_t<FLayers, Key>); at each level every
	 * installed instance whose type is IN that level receives Visitor(T&) via
	 * DispatchInstance (first match in the level wins). Levels serialize
	 * (barrier after each); instances within a level run in parallel on the
	 * inherited scheduler.
	 *
	 *   Layer.ForEach<Topo::TLevels_t<FLayers, FDefaultSlot>>([](auto& L) { L.Main(); });
	 */
	template <typename FLevels, typename TVisitor>
	void ForEach(TVisitor&& Visitor)
	{
		// Compile-time branch: if FLevels is an all-singleton type table → drive
		// via T::Get(); otherwise (instance type table) → dispatch against Layers.
		if constexpr (LayerDetail::TAllSingleton<FLevels>::value)
		{
			// singleton branch: every level type is a TSingleton → T::Get()
			Maho::ForEach<FLevels>(Maho::FSerialTraversePolicy{}, [&](auto LevelTag)
			{
				using FLevel = typename decltype(LevelTag)::Type;
				std::vector<std::function<void()>> Tasks;
				Tasks.reserve(FLevel::Count);
				Maho::ForEach<FLevel>(Maho::FSerialTraversePolicy{}, [&](auto TypeTag)
				{
					using T = typename decltype(TypeTag)::Type;
					Tasks.emplace_back([&] { Visitor(T::Get()); });
				});
				Parallel::FParallelScheduler::ForEach(Tasks, [](const std::function<void()>& T) { return T; });
			});
		}
		else
		{
			// instance branch: match installed Layers against each level's types
			Maho::ForEach<FLevels>(Maho::FSerialTraversePolicy{}, [this, &Visitor](auto LevelTag)
			{
				using FLevel = typename decltype(LevelTag)::Type;
				std::vector<std::function<void()>> Tasks;
				Tasks.reserve(Layers.size());
				for (FLayerBase* L : Layers)
				{
					Tasks.emplace_back([L, &Visitor]
					{
						DispatchInstance(FLevel{}, L, Visitor);
					});
				}
				Parallel::FParallelScheduler::ForEach(Tasks, [](const std::function<void()>& T) { return T; });
			});
		}
	}

private:
	FLayerBase* Load(std::string_view DLLPath)
	{
		auto& M = LoadedModules.emplace_back();
		if (M.Load(DLLPath))
		{
			using CreateFunction = FLayerBase* (*)();
			auto Create = M.GetProcAs<CreateFunction>("CreateLayer");
			if (Create)
			{
				return Create();
			}
		}
		LoadedModules.pop_back();
		return nullptr;
	}

private:
	std::vector<FLayerBase*> Layers;

	std::vector<FAssembly> LoadedModules;
};

} // namespace Maho

#define MAHO_DECLARE_LAYER(CustomBase, AppDLL)          \
public:                                                 \
	static Maho::FLayerBase* CreateLayer()          	\
	{                                                   \
		return new CustomBase();                        \
	}                                                   \
	static std::string_view GetModulePath()             \
	{                                                   \
		return AppDLL;                                  \
	}                                                   \

