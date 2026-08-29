#include <Engine/Engine.h>

#include <Core/Fatal.h>
#include <CLI/CLI.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Maho
{

FEngineLayer::~FEngineLayer() = default;

FEngineBase::FEngineBase() = default;

FEngineBase::~FEngineBase() = default;

void FEngineBase::ParseCommandLine(int Argc, char** Argv)
{
	// Normalize each "-key" (or "-key=value") to a "--key" long-option so CLI11
	// handles single-dash names without interpreting them as short-flag chains.
	std::vector<std::string> Normalized;
	Normalized.reserve(static_cast<std::size_t>(Argc) + 1);
	Normalized.push_back("maho");   // program name slot CLI11 consumes
	for (int I = 1; I < Argc; ++I)
	{
		const std::string Arg = Argv[I];
		if (Arg.size() < 2 || Arg[0] != '-')
		{
			continue;   // positional/non-hyphen junk - ignore
		}
		if (Arg[1] == '-')
		{
			// already a long option; keep as-is
			Normalized.push_back(Arg);
			continue;
		}

		// single-dash -> long-option. Bind the value inline so we never drop it.
		std::string Body = Arg.substr(1);
		if (Body.find('=') != std::string::npos)
		{
			Normalized.push_back("--" + Body);   // --key=value
			continue;
		}

		if (I + 1 < Argc && Argv[I + 1][0] != '-')
		{
			Normalized.push_back("--" + Body + "=" + Argv[I + 1]);   // --key value
			++I;
		}
		else
		{
			Normalized.push_back("--" + Body + "=true");   // bare flag -> true
		}
	}

	CLI::App App;
	App.allow_extras();

	// Discover unique option names so we can declare one CLI11 option per key.
	// CLI11 does the real tokenization / quoted-value work.
	std::vector<std::string> Keys;
	for (const std::string& Arg : Normalized)
	{
		if (Arg.size() < 2 || Arg[0] != '-')
		{
			continue;
		}
		std::string Sig = Arg[1] == '-' ? Arg.substr(2) : Arg.substr(1);
		const std::size_t Eq = Sig.find('=');
		if (Eq != std::string::npos)
		{
			Sig = Sig.substr(0, Eq);
		}
		Keys.push_back(Sig);
	}
	std::sort(Keys.begin(), Keys.end());
	Keys.erase(std::unique(Keys.begin(), Keys.end()), Keys.end());

	for (const std::string& Key : Keys)
	{
		// every discovered flag now carries a value (=true for bare flags), so a
		// single expected value binding is deterministic.
		App.add_option("--" + Key)->expected(1);
	}

	try
	{
		App.parse(Normalized);
	}
	catch (const CLI::ParseError&)
	{
		// Don't abort on bad input - read back whatever parsed.
	}

	// Read the parsed results back into the KV store.
	for (const std::string& Key : Keys)
	{
		CLI::Option* Opt = App.get_option_no_throw("--" + Key);
		if (Opt && Opt->count() > 0 && !Opt->results().empty())
		{
			Store[Key] = Opt->results().front();
		}
	}
}

int FEngineBase::Main()
{
	// Merge the layers installed in PreMain into Pipelines. Their Initialize is
	// driven by the InitGraph below (do NOT call FlushPendingUpdatePipelines here,
	// which would init them twice).
	for (FEngineLayer* P : PendingAdded)
	{
		Pipelines.push_back(P);
	}
	PendingAdded.clear();

	// Init pipeline: drive once with a temporary graph; layers mounting IEngineInitPipeline Initialize in dependency order.
	FLayerTaskGraph<IEngineInitPipeline, FEngineBase> InitGraph(Pool, *this);
	InitGraph.Init(Pipelines);
	if (!InitGraph.Compile())
	{
		ReportFatal("FEngineBase::Main: init pipeline Compile failed (missing dependency or cycle)");
	}
	InitGraph.Execute();
	InitGraph.Flush();

	// Tick pipeline: the main loop.
	FLayerTaskGraph<IEngineTickPipeline, FEngineBase> EngineGraph(Pool, *this);
	while (true)
	{
		EngineGraph.Flush();
		FlushPendingUpdatePipelines();

		EngineGraph.Init(Pipelines);
		if (!EngineGraph.Compile())
		{
			ReportFatal("FEngineBase::Main: tick pipeline Compile failed (missing dependency or cycle)");
		}
		EngineGraph.Execute();

		// An input layer (e.g. GameInputLayer) calls RequestExit inside Tick;
		// this frame's Execute has already finished, so check the exit flag here safely.
		if (bIsShuttingDown.load(std::memory_order_acquire))
		{
			EngineGraph.Flush();
			break;
		}
	}

	// Shutdown pipeline: drive once with a temporary graph; layers mounting IEngineShutdownPipeline Shutdown in dependency order.
	FLayerTaskGraph<IEngineShutdownPipeline, FEngineBase> ShutdownGraph(Pool, *this);
	ShutdownGraph.Init(Pipelines);
	if (!ShutdownGraph.Compile())
	{
		ReportFatal("FEngineBase::Main: shutdown pipeline Compile failed (missing dependency or cycle)");
	}
	ShutdownGraph.Execute();
	ShutdownGraph.Flush();

	// Delete feature instances first (virtual dtors live in their own DLLs), then release the DLLs.
	Features.clear();
	Modules.clear();

	return 0;
}

void FEngineBase::RequestExit()
{
	bIsShuttingDown.store(true, std::memory_order_release);
}

void FEngineBase::Install(FEngineLayer* Pipeline)
{
	PendingAdded.push_back(Pipeline);
}

void FEngineBase::Install(std::string_view DllPath, const char* FactorySymbol)
{
	auto Asm = std::make_unique<FAssembly>(DllPath);
	if (!Asm->IsLoaded())
	{
		return;
	}

	using CreateFn = FEngineLayer* (*)();
	auto Create = Asm->GetProcAs<CreateFn>(FactorySymbol);
	if (Create == nullptr)
	{
		return;
	}

	auto Layer = std::unique_ptr<FEngineLayer>(Create());
	if (!Layer)
	{
		return;
	}

	Install(Layer.get());
	Modules.push_back(std::move(Asm));
	Features.push_back(std::move(Layer));
}

void FEngineBase::RequestUninstall(FEngineLayer* Pipeline)
{
	if (Pipeline != nullptr)
	{
		PendingRemoveRequests.insert(Pipeline);
	}
}

void FEngineBase::TryUninstall(std::string_view LayerName)
{
	for (FLayerBase* L : Pipelines)
	{
		if (L->GetName() == LayerName)
		{
			RequestUninstall(static_cast<FEngineLayer*>(L));
			return;
		}
	}
}

void FEngineBase::FlushPendingUpdatePipelines()
{
	// 1. Apply pending installs: append to Pipelines, then drive the init pipeline
	//    over the newly added layers only (their Initialize/PreInit/PostInit run now).
	if (!PendingAdded.empty())
	{
		std::vector<FLayerBase*> NewLayers;
		NewLayers.reserve(PendingAdded.size());
		for (FEngineLayer* P : PendingAdded)
		{
			Pipelines.push_back(P);
			NewLayers.push_back(P);
		}
		PendingAdded.clear();

		FLayerTaskGraph<IEngineInitPipeline, FEngineBase> InitGraph(Pool, *this);
		InitGraph.Init(std::move(NewLayers));
		if (!InitGraph.Compile())
		{
			ReportFatal("FEngineBase::FlushPendingUpdatePipelines: install init pipeline Compile failed");
		}
		InitGraph.Execute();
		InitGraph.Flush();
	}

	// 2. Apply pending uninstalls: min-heap greedy unload drives the shutdown
	//    pipeline over the unloadable layers, then removes + deletes them.
	FlushUnload();
}

void FEngineBase::DeleteUnloaded(FEngineLayer* Layer)
{
	for (std::size_t I = 0; I < Features.size(); ++I)
	{
		if (Features[I].get() == Layer)
		{
			// Delete the feature first (virtual dtor lives in the DLL), then release the DLL.
			Features[I].reset();
			if (I < Modules.size())
			{
				Modules[I].reset();
			}
			return;
		}
	}
}

void FEngineBase::RebuildReverseDeps()
{
	ReverseDepCount.clear();

	// Initialize all active layer names to 0 (layers with no dependencies and no dependents are included too).
	for (FLayerBase* L : Pipelines)
	{
		ReverseDepCount[std::string(L->GetName())] = 0;
	}
	for (FEngineLayer* L : PendingAdded)
	{
		ReverseDepCount[std::string(L->GetName())] = 0;
	}

	// Accumulate: every dependency declared by an active layer -> depended-on count +1.
	for (FLayerBase* L : Pipelines)
	{
		for (const auto& [Stage, Deps] : L->GetDependencies())
		{
			(void)Stage;
			for (const auto& Dep : Deps)
			{
				ReverseDepCount[Dep.Name] += 1;
			}
		}
	}
	for (FEngineLayer* L : PendingAdded)
	{
		for (const auto& [Stage, Deps] : L->GetDependencies())
		{
			(void)Stage;
			for (const auto& Dep : Deps)
			{
				ReverseDepCount[Dep.Name] += 1;
			}
		}
	}
}

void FEngineBase::FlushUnload()
{
	if (PendingRemoveRequests.empty())
	{
		return;
	}
	RebuildReverseDeps();

	// name -> active layer pointer (only Pipelines; PendingAdded was already applied before this function).
	std::map<std::string, FEngineLayer*> ByName;
	for (FLayerBase* L : Pipelines)
	{
		ByName[std::string(L->GetName())] = static_cast<FEngineLayer*>(L);
	}

	// Min-heap: only holds layers "already requested for unload" (depended-on count, name).
	using HeapEntry = std::pair<int, std::string>;
	auto Cmp = [](const HeapEntry& A, const HeapEntry& B)
	{
		return A.first > B.first;   // min-heap
	};
	std::priority_queue<HeapEntry, std::vector<HeapEntry>, decltype(Cmp)> Heap(Cmp);
	for (FEngineLayer* L : PendingRemoveRequests)
	{
		const std::string Name = std::string(L->GetName());
		Heap.push({ ReverseDepCount[Name], Name });
	}

	// Greedy pass: collect every safely unloadable layer in dependency-safe order.
	std::vector<FLayerBase*> ToUnload;
	while (!Heap.empty())
	{
		const auto [Count, Name] = Heap.top();
		Heap.pop();

		// Stale entry: the count was already updated by an earlier pop.
		if (ReverseDepCount[Name] != Count)
		{
			continue;
		}
		auto It = ByName.find(Name);
		if (It == ByName.end())
		{
			continue;   // not in the active set (already unloaded).
		}
		FEngineLayer* Layer = It->second;
		if (!PendingRemoveRequests.count(Layer))
		{
			continue;   // already processed.
		}
		if (Count > 0)
		{
			break;   // depended on -> the heap only grows after this, abandon remaining requests.
		}
		ByName.erase(Name);
		PendingRemoveRequests.erase(Layer);
		ToUnload.push_back(Layer);

		// Update the depended-on counts of Layer's dependents (-1) and push them
		// back into the heap to trigger chained unload.
		for (const auto& [Stage, Deps] : Layer->GetDependencies())
		{
			(void)Stage;
			for (const auto& Dep : Deps)
			{
				const int NewCount = ReverseDepCount[Dep.Name] - 1;
				ReverseDepCount[Dep.Name] = NewCount;
				Heap.push({ NewCount, Dep.Name });
			}
		}
	}

	// Abandon the remaining requests of this batch that cannot be safely unloaded (no cross-frame carry-over).
	PendingRemoveRequests.clear();

	if (ToUnload.empty())
	{
		return;
	}

	// Drive the shutdown pipeline over the unloadable layers (their Shutdown
	// stages run in dependency order), then remove + delete them.
	FLayerTaskGraph<IEngineShutdownPipeline, FEngineBase> ShutdownGraph(Pool, *this);
	ShutdownGraph.Init(std::move(ToUnload));
	if (!ShutdownGraph.Compile())
	{
		ReportFatal("FEngineBase::FlushUnload: shutdown pipeline Compile failed");
	}
	ShutdownGraph.Execute();
	ShutdownGraph.Flush();

	for (FLayerBase* L : ToUnload)
	{
		Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), L), Pipelines.end());
		DeleteUnloaded(static_cast<FEngineLayer*>(L));
	}
}

bool FEngineBase::Has(std::string_view Key) const
{
	return Store.find(std::string(Key)) != Store.end();
}

std::string FEngineBase::Get(std::string_view Key) const
{
	auto It = Store.find(std::string(Key));
	return It != Store.end() ? It->second : std::string{};
}

bool FEngineBase::GetBool(std::string_view Key) const
{
	const std::string Value = Get(Key);
	return Value == "true" || Value == "1" || Value == "yes" || Value == "on";
}

int FEngineBase::GetInt(std::string_view Key) const
{
	const std::string Value = Get(Key);
	if (Value.empty())
	{
		return 0;
	}
	try
	{
		return std::stoi(Value);
	}
	catch (...)
	{
		return 0;
	}
}

} // namespace Maho
