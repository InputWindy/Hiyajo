#include <Core/App.h>

#include <Core/Misc/Console.h>
#include <Core/Misc/Fatal.h>
#include <Core/Misc/Log.h>
#include <Core/Misc/Paths.h>
#include <Core/Misc/Timer.h>
#include <Core/Engine/EngineStage.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <utility>

namespace Maho
{

namespace
{

static TAutoConsoleVariable GCVarFixedDeltaSeconds(
	"t.FixedDeltaSeconds",
	1.0f / 50.0f,
	"Fixed simulation step in seconds (0 disables fixed updates)");

static TAutoConsoleVariable GCVarMaxFixedUpdatesPerFrame(
	"t.MaxFixedUpdatesPerFrame",
	5,
	"Max fixed updates per frame (spiral-of-death clamp)");

static TAutoConsoleVariable GCVarMaxDeltaSeconds(
	"t.MaxDeltaSeconds",
	0.25f,
	"Clamp frame delta seconds after hitch / debugger pause");

void BootstrapAppLogging(FApp& App)
{
	FLogConfig LogConfig;
	LogConfig.CoreLoggerName = "Maho";
	LogConfig.ClientLoggerName = "App";
	LogConfig.bEnableConsole = false;
	LogConfig.bEnableFile = false;
	LogConfig.bEnableEditorCapture = true;
	App.GetLog().Initialize(LogConfig);
}

void ApplyAppLoggingFromConfig(FApp& App, const FConfig& Config)
{
	FLogConfig LogConfig;
	LogConfig.CoreLoggerName = "Maho";
	LogConfig.ClientLoggerName = Config.ApplicationName.empty() ? "App" : Config.ApplicationName;
	LogConfig.LogDirectory = Config.SavedDir + "/Logs";
	LogConfig.bEnableConsole = false;
	LogConfig.bEnableFile = true;
	LogConfig.bEnableEditorCapture = true;
	App.GetLog().Initialize(LogConfig);
}

void ShutdownAppLogging(FApp& App)
{
	for (const FTimerDataPackage& Report : App.GetTimer().QueryAll())
	{
		if (!Report.Samples.empty())
		{
			MAHO_CORE_INFO("{}", Report.Serialize());
		}
	}
	App.GetLog().Shutdown();
}

int LoadProjectEngineIni(FApp& App, FConfig& Config)
{
	const std::string IniPath = Config.ProjectConfigDir + "/DefaultEngine.ini";
	const int Applied = App.GetConsole().LoadConsoleVariablesFromIni(IniPath);
	ApplyEngineCVarsToConfig(Config);
	return Applied;
}

template <typename TObject>
void TopoSortPeers(
	const std::vector<TObject*>& StableOrder,
	const std::unordered_map<std::type_index, std::vector<FExtensionStageDep>>& Deps,
	std::vector<TObject*>& OutOrder)
{
	OutOrder.clear();
	const std::size_t Count = StableOrder.size();
	if (Count == 0)
	{
		return;
	}

	std::vector<std::type_index> Types;
	Types.reserve(Count);
	std::unordered_map<std::type_index, std::size_t> TypeToIndex;
	for (std::size_t Index = 0; Index < Count; ++Index)
	{
		const std::type_index Type(typeid(*StableOrder[Index]));
		Types.push_back(Type);
		TypeToIndex.emplace(Type, Index);
	}

	std::vector<int> InDegree(Count, 0);
	std::vector<std::vector<std::size_t>> Adj(Count);

	for (std::size_t Self = 0; Self < Count; ++Self)
	{
		const auto DepIt = Deps.find(Types[Self]);
		if (DepIt == Deps.end())
		{
			continue;
		}
		for (const FExtensionStageDep& Edge : DepIt->second)
		{
			const auto DepIndexIt = TypeToIndex.find(Edge.Type);
			if (DepIndexIt == TypeToIndex.end())
			{
				continue;
			}
			const std::size_t Dep = DepIndexIt->second;
			if (Dep == Self)
			{
				continue;
			}
			Adj[Dep].push_back(Self);
			++InDegree[Self];
		}
	}

	std::queue<std::size_t> Ready;
	for (std::size_t Index = 0; Index < Count; ++Index)
	{
		if (InDegree[Index] == 0)
		{
			Ready.push(Index);
		}
	}

	OutOrder.reserve(Count);
	while (!Ready.empty())
	{
		const std::size_t Index = Ready.front();
		Ready.pop();
		OutOrder.push_back(StableOrder[Index]);
		for (std::size_t Next : Adj[Index])
		{
			if (--InDegree[Next] == 0)
			{
				Ready.push(Next);
			}
		}
	}

	if (OutOrder.size() != Count)
	{
		MAHO_CORE_ERROR("FApp: TDependsPack cycle detected; falling back to registration order");
		OutOrder = StableOrder;
	}
}

} // namespace

bool FApp::InvokeStage(IEngineExtension& Extension, EEngineStage Stage)
{
	Extension.SetCurrentStage(Stage);
	return Extension.ExecuteStage(Stage);
}

FApp* GApp = nullptr;

FApp::FApp()
{
	GApp = this;
	BootstrapAppLogging(*this);
	MAHO_CORE_INFO("FApp core services ready (Log, Console, Timer)");
}

FApp::~FApp()
{
	ClearExtensions();
	if (Log.IsInitialized())
	{
		Log.Shutdown();
	}
	if (GApp == this)
	{
		GApp = nullptr;
	}
}

void FApp::Configure(FConfig& /*OutConfig*/)
{
}

bool FApp::PreInitialize()
{
	return true;
}

bool FApp::PostInitialize()
{
	return true;
}

void FApp::OnRequestExit()
{
	if (AppState != EAppState::Running)
	{
		return;
	}

	MAHO_CORE_INFO("FApp::OnRequestExit — leaving Game loop");
	AppState = EAppState::WaitForExit;
}

std::vector<IEngineExtension*> FApp::SnapshotExtensions() const
{
	std::vector<IEngineExtension*> Out;
	Out.reserve(Extensions.size());
	for (const std::unique_ptr<IEngineExtension>& Extension : Extensions)
	{
		if (Extension)
		{
			Out.push_back(Extension.get());
		}
	}
	return Out;
}

std::vector<FExtensionDepEdgeView> FApp::SnapshotExtensionDeps(EEngineStage Stage) const
{
	std::vector<FExtensionDepEdgeView> Out;
	const auto StageIndex = static_cast<std::size_t>(Stage);
	if (StageIndex >= static_cast<std::size_t>(EEngineStage::COUNT))
	{
		return Out;
	}

	const auto& Deps = ExtensionDeps[StageIndex];
	for (const auto& Pair : Deps)
	{
		const auto SelfIt = ExtensionsByType.find(Pair.first);
		if (SelfIt == ExtensionsByType.end() || !SelfIt->second)
		{
			continue;
		}

		for (const FExtensionStageDep& Edge : Pair.second)
		{
			const auto DepIt = ExtensionsByType.find(Edge.Type);
			if (DepIt == ExtensionsByType.end() || !DepIt->second)
			{
				continue;
			}

			FExtensionDepEdgeView View;
			View.Predecessor = DepIt->second;
			View.Successor = SelfIt->second;
			View.Strength = Edge.Strength;
			Out.push_back(View);
		}
	}
	return Out;
}

bool FApp::RunExtensionInitFamily(IEngineExtension& Extension)
{
	static constexpr EEngineStage InitStages[] =
	{
		EEngineStage::PreInit,
		EEngineStage::Init,
		EEngineStage::PostInit,
	};

	for (EEngineStage Stage : InitStages)
	{
		if (!InvokeStage(Extension, Stage))
		{
			MAHO_CORE_ERROR(
				"FApp: extension '{}' failed at {}",
				Extension.GetName() ? Extension.GetName() : "?",
				static_cast<int>(Stage));
			return false;
		}
	}
	return true;
}

void FApp::RunExtensionAttach(IEngineExtension& Extension)
{
	InvokeStage(Extension, EEngineStage::Attach);
}

void FApp::RegisterExtensionInternal(
	std::unique_ptr<IEngineExtension> Extension,
	std::type_index TypeKey,
	EExtensionPriority Priority)
{
	if (!Extension)
	{
		return;
	}

	IEngineExtension* Raw = Extension.get();
	Raw->SetPriority(Priority);

	if (AppState == EAppState::Running)
	{
		PendingAdd.push_back(std::move(Extension));
		PendingAddTypes.push_back(TypeKey);
		PendingAddPriorities.push_back(Priority);
		return;
	}

	if (ExtensionsByType.find(TypeKey) != ExtensionsByType.end())
	{
		MAHO_CORE_ERROR(
			"FApp::RegisterExtension: duplicate type for '{}'",
			Raw->GetName() ? Raw->GetName() : "?");
		return;
	}

	ExtensionsByType[TypeKey] = Raw;
	Extensions.push_back(std::move(Extension));

	if (bLifecycleStarted)
	{
		if (!RunExtensionInitFamily(*Raw))
		{
			ExtensionsByType.erase(TypeKey);
			Extensions.pop_back();
			return;
		}
		RunExtensionAttach(*Raw);
	}
}

void FApp::RequestRemoveExtension(IEngineExtension* Extension)
{
	if (!Extension)
	{
		return;
	}
	PendingRemove.push_back(Extension);
}

void FApp::ClearExtensions()
{
	for (auto It = Extensions.rbegin(); It != Extensions.rend(); ++It)
	{
		if (*It)
		{
			InvokeStage(**It, EEngineStage::Detach);
		}
	}
	Extensions.clear();
	ExtensionsByType.clear();
	StageOrder.clear();
	PendingAdd.clear();
	PendingAddTypes.clear();
	PendingAddPriorities.clear();
	PendingRemove.clear();
	RuntimeMounted.clear();
	for (auto& DepMap : ExtensionDeps)
	{
		DepMap.clear();
	}
	bLifecycleStarted = false;
}

void FApp::EraseExtension(IEngineExtension* Extension)
{
	if (!Extension)
	{
		return;
	}

	RuntimeMounted.erase(Extension);
	ExtensionsByType.erase(std::type_index(typeid(*Extension)));
	Extensions.erase(
		std::remove_if(
			Extensions.begin(),
			Extensions.end(),
			[Extension](const std::unique_ptr<IEngineExtension>& Ptr)
			{
				return Ptr.get() == Extension;
			}),
		Extensions.end());
}

void FApp::FlushPendingMounts()
{
	for (IEngineExtension* Remove : PendingRemove)
	{
		if (!Remove)
		{
			continue;
		}

		// Running-time unmount: Attach … Detach only (PrepareExit/Shutdown are App exit).
		InvokeStage(*Remove, EEngineStage::Detach);
		EraseExtension(Remove);
	}
	PendingRemove.clear();

	if (PendingAdd.empty())
	{
		return;
	}

	for (std::size_t Index = 0; Index < PendingAdd.size(); ++Index)
	{
		std::unique_ptr<IEngineExtension>& Add = PendingAdd[Index];
		if (!Add)
		{
			continue;
		}

		const std::type_index TypeKey =
			Index < PendingAddTypes.size() ? PendingAddTypes[Index] : std::type_index(typeid(*Add));
		const EExtensionPriority Priority =
			Index < PendingAddPriorities.size() ? PendingAddPriorities[Index] : EExtensionPriority::Layer;

		IEngineExtension* Raw = Add.get();
		Raw->SetPriority(Priority);

		if (ExtensionsByType.find(TypeKey) != ExtensionsByType.end())
		{
			MAHO_CORE_ERROR(
				"FApp::FlushPendingMounts: duplicate '{}'",
				Raw->GetName() ? Raw->GetName() : "?");
			Add.reset();
			continue;
		}

		ExtensionsByType[TypeKey] = Raw;
		Extensions.push_back(std::move(Add));
		RunExtensionAttach(*Raw);
		RuntimeMounted.insert(Raw);
	}
	PendingAdd.clear();
	PendingAddTypes.clear();
	PendingAddPriorities.clear();
}

void FApp::DetachRuntimeMountedExtensions()
{
	if (RuntimeMounted.empty())
	{
		return;
	}

	std::vector<IEngineExtension*> StillMounted(RuntimeMounted.begin(), RuntimeMounted.end());
	RuntimeMounted.clear();

	for (IEngineExtension* Extension : StillMounted)
	{
		if (!Extension)
		{
			continue;
		}

		bool bAlive = false;
		for (const std::unique_ptr<IEngineExtension>& Ptr : Extensions)
		{
			if (Ptr.get() == Extension)
			{
				bAlive = true;
				break;
			}
		}
		if (!bAlive)
		{
			continue;
		}

		InvokeStage(*Extension, EEngineStage::Detach);
		EraseExtension(Extension);
	}
}

void FApp::AssertExtensionDepsPresent(EEngineStage Stage) const
{
	const auto StageIndex = static_cast<std::size_t>(Stage);
	const auto& Deps = ExtensionDeps[StageIndex];

	for (const auto& Pair : Deps)
	{
		const auto SelfIt = ExtensionsByType.find(Pair.first);
		if (SelfIt == ExtensionsByType.end() || !SelfIt->second)
		{
			continue;
		}

		for (const FExtensionStageDep& Edge : Pair.second)
		{
			if (ExtensionsByType.find(Edge.Type) != ExtensionsByType.end())
			{
				continue;
			}

			if (Edge.Strength == EExtensionDepStrength::Weak)
			{
				continue;
			}

			const char* SelfName = SelfIt->second->GetName();
			char Buffer[512] = {};
			std::snprintf(
				Buffer,
				sizeof(Buffer),
				"FApp: extension '%s' missing required dependency '%s' for EEngineStage(%d)",
				SelfName ? SelfName : "?",
				Edge.Type.name(),
				static_cast<int>(Stage));
			ReportFatal(Buffer);
		}
	}
}

void FApp::RebuildStageOrder(EEngineStage Stage)
{
	AssertExtensionDepsPresent(Stage);

	const auto StageIndex = static_cast<std::size_t>(Stage);
	const auto& Deps = ExtensionDeps[StageIndex];

	std::vector<IEngineExtension*> Bands[3];
	for (std::unique_ptr<IEngineExtension>& Extension : Extensions)
	{
		if (!Extension)
		{
			continue;
		}
		const int Band = static_cast<int>(Extension->GetPriority());
		if (Band < 0 || Band > 2)
		{
			Bands[0].push_back(Extension.get());
			continue;
		}
		Bands[Band].push_back(Extension.get());
	}

	StageOrder.clear();
	for (int Band = 0; Band < 3; ++Band)
	{
		std::vector<IEngineExtension*> Sorted;
		TopoSortPeers(Bands[Band], Deps, Sorted);
		StageOrder.insert(StageOrder.end(), Sorted.begin(), Sorted.end());
	}
}

bool FApp::InitRegisteredExtensions()
{
	RebuildStageOrder(EEngineStage::Init);

	static constexpr EEngineStage InitStages[] =
	{
		EEngineStage::PreInit,
		EEngineStage::Init,
		EEngineStage::PostInit,
	};

	for (EEngineStage Stage : InitStages)
	{
		if (Stage == EEngineStage::Init)
		{
			RebuildStageOrder(EEngineStage::Init);
		}
		for (IEngineExtension* Extension : StageOrder)
		{
			if (!Extension)
			{
				continue;
			}
			if (!InvokeStage(*Extension, Stage))
			{
				MAHO_CORE_ERROR(
					"FApp: extension '{}' failed at {}",
					Extension->GetName() ? Extension->GetName() : "?",
					static_cast<int>(Stage));
				return false;
			}
		}
	}
	return true;
}

void FApp::AttachRegisteredExtensions()
{
	RebuildStageOrder(EEngineStage::BeginFrame);
	for (IEngineExtension* Extension : StageOrder)
	{
		if (Extension)
		{
			RunExtensionAttach(*Extension);
		}
	}
}

void FApp::ShutdownRegisteredExtensions()
{
	RebuildStageOrder(EEngineStage::Shutdown);
	for (IEngineExtension* Extension : StageOrder)
	{
		if (!Extension)
		{
			continue;
		}
		InvokeStage(*Extension, EEngineStage::PrepareExit);
		InvokeStage(*Extension, EEngineStage::Detach);
		InvokeStage(*Extension, EEngineStage::Shutdown);
	}
}

bool FApp::Initialize()
{
	Configure(Config);
	FPaths::Initialize(Config);

	const int IniApplied = LoadProjectEngineIni(*this, Config);
	if (IniApplied < 0)
	{
		MAHO_CORE_WARN(
			"DefaultEngine.ini not found (looked for '{}/DefaultEngine.ini') — using CVar defaults",
			Config.ProjectConfigDir);
	}
	else
	{
		MAHO_CORE_INFO(
			"Loaded/queued {} CVar override(s) from '{}/DefaultEngine.ini'",
			IniApplied,
			Config.ProjectConfigDir);
	}

	ApplyAppLoggingFromConfig(*this, Config);

	MAHO_CORE_INFO("FPaths: ProjectDir = {}", Config.ProjectDir);
	MAHO_CORE_INFO("FPaths: EngineDir  = {}", Config.EngineDir);

	if (!PreInitialize())
	{
		MAHO_CORE_ERROR("FApp::PreInitialize failed");
		ClearExtensions();
		ShutdownAppLogging(*this);
		return false;
	}
	if (!InitRegisteredExtensions())
	{
		ShutdownRegisteredExtensions();
		ClearExtensions();
		ShutdownAppLogging(*this);
		return false;
	}

	AttachRegisteredExtensions();
	bLifecycleStarted = true;

	if (!PostInitialize())
	{
		MAHO_CORE_ERROR("FApp::PostInitialize failed");
		ShutdownRegisteredExtensions();
		Extensions.clear();
		ExtensionsByType.clear();
		for (auto& DepMap : ExtensionDeps)
		{
			DepMap.clear();
		}
		StageOrder.clear();
		bLifecycleStarted = false;
		ShutdownAppLogging(*this);
		return false;
	}

	LastFrameTimeSeconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now().time_since_epoch()).count();

	return true;
}

void FApp::Shutdown()
{
	// Runtime mounts: Attach…Detach only — auto-Detach if never explicitly removed.
	DetachRuntimeMountedExtensions();

	ShutdownRegisteredExtensions();
	Extensions.clear();
	ExtensionsByType.clear();
	for (auto& DepMap : ExtensionDeps)
	{
		DepMap.clear();
	}
	StageOrder.clear();
	PendingAdd.clear();
	PendingAddTypes.clear();
	PendingAddPriorities.clear();
	PendingRemove.clear();
	RuntimeMounted.clear();
	bLifecycleStarted = false;
}

void FApp::UpdateAppState()
{
	const double NowSeconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now().time_since_epoch()).count();

	float NewDelta = static_cast<float>(NowSeconds - LastFrameTimeSeconds);
	LastFrameTimeSeconds = NowSeconds;

	if (NewDelta < 0.0f)
	{
		NewDelta = 0.0f;
	}

	const float MaxDeltaSeconds = (std::max)(0.0f, GCVarMaxDeltaSeconds.GetValue());
	if (MaxDeltaSeconds > 0.0f)
	{
		NewDelta = (std::min)(NewDelta, MaxDeltaSeconds);
	}

	DeltaSeconds = NewDelta;
	++FrameIndex;

	const float FixedDelta = GCVarFixedDeltaSeconds.GetValue();
	const int MaxFixed = GCVarMaxFixedUpdatesPerFrame.GetValue();
	FixedDeltaSeconds = FixedDelta;
	FixedStepsRemaining = 0;
	if (FixedDelta > 0.0f && MaxFixed > 0)
	{
		FixedUpdateAccumulator += DeltaSeconds;
		int Steps = 0;
		while (FixedUpdateAccumulator >= FixedDelta && Steps < MaxFixed)
		{
			FixedUpdateAccumulator -= FixedDelta;
			++Steps;
		}
		if (Steps >= MaxFixed)
		{
			FixedUpdateAccumulator = 0.0f;
		}
		FixedStepsRemaining = Steps;
	}
}

void FApp::Tick()
{
	UpdateAppState();

	auto TickGroup = [this](EEngineStage Stage)
	{
		for (IEngineExtension* Extension : StageOrder)
		{
			if (Extension)
			{
				InvokeStage(*Extension, Stage);
			}
		}
	};

	FlushPendingMounts();
	RebuildStageOrder(EEngineStage::BeginFrame);

	static constexpr EEngineStage Stages[] =
	{
		EEngineStage::BeginFrame,
		EEngineStage::Tick,
		EEngineStage::EndFrame,
		EEngineStage::PreRender,
		EEngineStage::Render,
		EEngineStage::PostRender,
	};

	for (EEngineStage Stage : Stages)
	{
		TickGroup(Stage);
	}
}

void FApp::Run()
{
	if (!Initialize())
	{
		return;
	}

	AppState = EAppState::Running;
	MAHO_CORE_INFO("FApp: Game loop start (TickGroups + RenderServer)");

	while (AppState == EAppState::Running)
	{
		Tick();
	}

	MAHO_CORE_INFO("FApp: Game loop end (state={})", static_cast<int>(AppState));
	Shutdown();
	ShutdownAppLogging(*this);
	AppState = EAppState::Stopped;
}

} // namespace Maho
