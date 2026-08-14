#pragma once

#include <Core/Misc/DependsPack.h>
#include <Core/Misc/Console.h>
#include <Core/Engine/Engine.h>
#include <Core/Misc/Export.h>
#include <Core/Misc/Log.h>
#include <Core/Misc/Timer.h>
#include <Core/Engine/EngineExtension.h>
#include <Core/Engine/EngineStage.h>
#include <Core/Misc/TypeList.h>

#include <cstdint>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Maho
{

enum class EAppState : std::uint8_t
{
	Stopped = 0,
	Running,
	WaitForExit,
};

/** Resolved depends edge for editors (Dep completes before Self). */
struct FExtensionDepEdgeView
{
	IEngineExtension* Predecessor = nullptr;
	IEngineExtension* Successor = nullptr;
	EExtensionDepStrength Strength = EExtensionDepStrength::Strong;
};

/**
 * Application shell: config / log / CVars / timer on the shell;
 * Window / RHI / Script / WorkerPool live as IEngineExtension (access via GetExtension<T>).
 * Main-thread TickGroups over a single Extensions list ordered by Priority then depends.
 */
class MAHO_API FAppBase
{
public:
	FAppBase();
	virtual ~FAppBase();

	FAppBase(const FAppBase&) = delete;
	FAppBase& operator=(const FAppBase&) = delete;

	void Run();

	[[nodiscard]] FConfig& GetConfig() { return Config; }
	[[nodiscard]] const FConfig& GetConfig() const { return Config; }

	[[nodiscard]] FLog& GetLog() { return Log; }
	[[nodiscard]] FConsole& GetConsole() { return Console; }
	[[nodiscard]] FTimer& GetTimer() { return Timer; }

	[[nodiscard]] std::uint64_t GetFrameIndex() const { return FrameIndex; }
	[[nodiscard]] float GetDeltaSeconds() const { return DeltaSeconds; }
	[[nodiscard]] float GetFixedDeltaSeconds() const { return FixedDeltaSeconds; }
	[[nodiscard]] int GetFixedStepsRemaining() const { return FixedStepsRemaining; }

	[[nodiscard]] EAppState GetState() const { return AppState; }

	template <typename T>
	[[nodiscard]] T* GetExtension()
	{
		static_assert(std::is_base_of_v<IEngineExtension, T>, "T must derive from IEngineExtension");
		const auto It = ExtensionsByType.find(std::type_index(typeid(T)));
		return It != ExtensionsByType.end() ? static_cast<T*>(It->second) : nullptr;
	}

	template <typename T>
	[[nodiscard]] const T* GetExtension() const
	{
		return const_cast<FAppBase*>(this)->GetExtension<T>();
	}

	/** Exit from window close (ShouldClose) or headless auto-exit. */
	void OnRequestExit();

	/** Live extension pointers (registration order). Editor / debug visualizers. */
	[[nodiscard]] std::vector<IEngineExtension*> SnapshotExtensions() const;

	/**
	 * Depends edges for one stage: Predecessor (Dep) runs before Successor (Self).
	 * Missing peers (weak) are omitted.
	 */
	[[nodiscard]] std::vector<FExtensionDepEdgeView> SnapshotExtensionDeps(EEngineStage Stage) const;

protected:
	template <typename T, typename... TArgs>
	T& RegisterExtension(EExtensionPriority Priority, TArgs&&... Args)
	{
		static_assert(std::is_base_of_v<IEngineExtension, T>, "T must derive from IEngineExtension");
		auto Extension = std::make_unique<T>(std::forward<TArgs>(Args)...);
		T& Ref = *Extension;
		CollectDependsFromType<T>(std::type_index(typeid(T)));
		RegisterExtensionInternal(std::move(Extension), std::type_index(typeid(T)), Priority);
		return Ref;
	}

	/** Register a pre-built extension instance under an explicit type key. */
	IEngineExtension& RegisterExtensionInstance(
		std::unique_ptr<IEngineExtension> Extension,
		std::type_index TypeKey,
		EExtensionPriority Priority);

	virtual void Configure(FConfig& OutConfig);
	virtual bool PreInitialize();
	virtual bool PostInitialize();

	/** Per-frame application hook. The world/render framing is owned by subclasses. */
	virtual void Tick() = 0;

	/** Dispatch one stage to every registered extension in dependency order. */
	void DispatchStageToExtensions(EEngineStage Stage);

	void ClearExtensions();
	void RequestRemoveExtension(IEngineExtension* Extension);

private:
	template <typename TList>
	static void CollectTypeListDeps(
		std::vector<FExtensionStageDep>& Out,
		EExtensionDepStrength Strength)
	{
		CollectTypeListDeps(Out, Strength, static_cast<TList*>(nullptr));
	}

	static void CollectTypeListDeps(
		std::vector<FExtensionStageDep>& /*Out*/,
		EExtensionDepStrength /*Strength*/,
		TTypeList<>*)
	{
	}

	template <typename THead, typename... TRest>
	static void CollectTypeListDeps(
		std::vector<FExtensionStageDep>& Out,
		EExtensionDepStrength Strength,
		TTypeList<THead, TRest...>*)
	{
		Out.push_back(FExtensionStageDep{std::type_index(typeid(THead)), Strength});
		CollectTypeListDeps(Out, Strength, static_cast<TTypeList<TRest...>*>(nullptr));
	}

	template <typename T>
	void CollectDependsFromType(std::type_index SelfType)
	{
		using FPack = typename TResolveDependsPack<T>::Type;
		FPack::BuildGraph(
			[this, SelfType](auto StageKey, auto* ListPtr, EExtensionDepStrength Strength)
			{
				using TList = std::remove_pointer_t<decltype(ListPtr)>;
				std::vector<FExtensionStageDep> Deps;
				CollectTypeListDeps<TList>(Deps, Strength);
				using FKey = decltype(StageKey);
				if constexpr (std::is_same_v<FKey, EEngineStage>)
				{
					const auto Index = static_cast<std::size_t>(StageKey);
					if (Index < static_cast<std::size_t>(EEngineStage::COUNT))
					{
						ExtensionDeps[Index][SelfType] = std::move(Deps);
					}
				}
			});
	}

	void RegisterExtensionInternal(
		std::unique_ptr<IEngineExtension> Extension,
		std::type_index TypeKey,
		EExtensionPriority Priority);

	[[nodiscard]] bool RunExtensionInitFamily(IEngineExtension& Extension);
	void RunExtensionAttach(IEngineExtension& Extension);

	bool Initialize();
	void Shutdown();
	void UpdateAppState();

	void FlushPendingMounts();

	[[nodiscard]] bool InitRegisteredExtensions();
	void AttachRegisteredExtensions();
	void DetachRuntimeMountedExtensions();
	void ShutdownRegisteredExtensions();
	void EraseExtension(IEngineExtension* Extension);
	void AssertExtensionDepsPresent(EEngineStage Stage) const;
	void RebuildStageOrder(EEngineStage Stage);
	static bool InvokeStage(IEngineExtension& Extension, EEngineStage Stage);

	FConfig Config;
	FLog Log;
	FConsole Console;
	FTimer Timer;

	std::vector<std::unique_ptr<IEngineExtension>> Extensions;
	std::unordered_map<std::type_index, IEngineExtension*> ExtensionsByType;
	std::unordered_map<std::type_index, std::vector<FExtensionStageDep>> ExtensionDeps[static_cast<std::size_t>(EEngineStage::COUNT)];

	std::vector<IEngineExtension*> StageOrder;
	std::vector<std::unique_ptr<IEngineExtension>> PendingAdd;
	std::vector<std::type_index> PendingAddTypes;
	std::vector<EExtensionPriority> PendingAddPriorities;
	std::vector<IEngineExtension*> PendingRemove;
	/** Extensions Attach'd while Running; auto-Detach on exit if still mounted. */
	std::unordered_set<IEngineExtension*> RuntimeMounted;

	bool bLifecycleStarted = false;
	EAppState AppState = EAppState::Stopped;
	float DeltaSeconds = 0.0f;
	float FixedDeltaSeconds = 0.0f;
	float FixedUpdateAccumulator = 0.0f;
	int FixedStepsRemaining = 0;
	double LastFrameTimeSeconds = 0.0;
	std::uint64_t FrameIndex = 0;
};

MAHO_API extern FAppBase* GApp;

FAppBase* CreateApplication();

} // namespace Maho
