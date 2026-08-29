#include <Engine/Engine.h>

namespace Maho
{

void FEngineBase::ParseCommandLine(int Argc, char** Argv)
{
	LaunchArgc = Argc;
	LaunchArgv = Argv;
}

int FEngineBase::Main()
{
	// Apply the layers installed in PreMain (must be effective before InitGraph).
	FlushPendingUpdatePipelines();

	// Init pipeline: drive once with a temporary graph; layers mounting IEngineInitPipeline Initialize in dependency order.
	FLayerTaskGraph<IEngineInitPipeline, FEngineBase> InitGraph(Pool, *this);
	InitGraph.Init(Pipelines);
	if (InitGraph.Compile())
	{
		InitGraph.Execute();
		InitGraph.Flush();

		FLayerTaskGraph<IEngineTickPipeline, FEngineBase> EngineGraph(Pool, *this);

		while (true)
		{
			EngineGraph.Flush();
			FlushPendingUpdatePipelines();

			EngineGraph.Init(Pipelines);
			if (EngineGraph.Compile())
			{
				EngineGraph.Execute();
			}
			else
			{
				EngineGraph.Flush();
				break;
			}
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
		if (ShutdownGraph.Compile())
		{
			ShutdownGraph.Execute();
		}
		ShutdownGraph.Flush();

		// Delete feature instances first (virtual dtors live in their own DLLs), then release the DLLs.
		Features.clear();
		Modules.clear();
	}

	return 0;
}

void FEngineBase::RequestExit()
{
	bIsShuttingDown.store(true, std::memory_order_release);
}

FEngineLayer* FEngineBase::FindLayer(std::string_view LayerName)
{
	for (FLayerBase* L : Pipelines)
	{
		if (L->GetName() == LayerName)
		{
			return static_cast<FEngineLayer*>(L);
		}
	}
	return nullptr;
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
	for (FEngineLayer* P : PendingAdded)
	{
		Pipelines.push_back(P);
	}
	PendingAdded.clear();

	FlushUnload();   // min-heap greedy unload (batch application of random uninstall requests)
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
		// Safely unload Layer: remove from the pipeline + remove from the request set.
		Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), Layer), Pipelines.end());
		ByName.erase(Name);
		PendingRemoveRequests.erase(Layer);

		// First update the depended-on counts of Layer's dependents (-1) and push
		// them back into the heap to trigger chained unload --
		// GetDependencies() must be read before deleting Layer.
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

		// Finally delete the instance + FreeLibrary.
		DeleteUnloaded(Layer);
	}

	// Abandon the remaining requests of this batch that cannot be safely unloaded (no cross-frame carry-over).
	PendingRemoveRequests.clear();
}

} // namespace Maho
