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
	// 应用 PreMain 里 Install 的挂起层（InitGraph 前必须生效）。
	FlushPendingUpdatePipelines();

	// 初始化管线：临时图驱动一次，挂 IEngineInitPipeline 的 layer 按依赖序 Initialize。
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
			// 输入层（如 GameInputLayer）在 Tick 里调用 RequestExit 后，
			// 本帧 Execute 已结束，此处安全检查退出标志。
			if (bIsShuttingDown.load(std::memory_order_acquire))
			{
				EngineGraph.Flush();
				break;
			}
		}

		// 卸载管线：临时图驱动一次，挂 IEngineShutdownPipeline 的 layer 按依赖序 Shutdown。
		FLayerTaskGraph<IEngineShutdownPipeline, FEngineBase> ShutdownGraph(Pool, *this);
		ShutdownGraph.Init(Pipelines);
		if (ShutdownGraph.Compile())
		{
			ShutdownGraph.Execute();
		}
		ShutdownGraph.Flush();

		// 先删 feature 实例（虚析构在各自 DLL），再释放 DLL。
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

	FlushUnload();   // 小顶堆贪心卸载（随机卸载请求的批量应用）
}

void FEngineBase::DeleteUnloaded(FEngineLayer* Layer)
{
	for (std::size_t I = 0; I < Features.size(); ++I)
	{
		if (Features[I].get() == Layer)
		{
			// 先 delete feature（虚析构在 DLL），再释放 DLL。
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

	// 活跃层名全量初始化为 0（无依赖/无被依赖的层也在内）。
	for (FLayerBase* L : Pipelines)
	{
		ReverseDepCount[std::string(L->GetName())] = 0;
	}
	for (FEngineLayer* L : PendingAdded)
	{
		ReverseDepCount[std::string(L->GetName())] = 0;
	}

	// 累加：每个活跃层声明的依赖 → 被依赖者计数 +1。
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

	// name → 活跃层指针（仅 Pipelines；PendingAdded 已在本函数前应用）。
	std::map<std::string, FEngineLayer*> ByName;
	for (FLayerBase* L : Pipelines)
	{
		ByName[std::string(L->GetName())] = static_cast<FEngineLayer*>(L);
	}

	// 小顶堆：只装"已请求卸载"的层 (被依赖数, name)。
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

		// 过期条目：计数已被更早的弹出更新过。
		if (ReverseDepCount[Name] != Count)
		{
			continue;
		}
		auto It = ByName.find(Name);
		if (It == ByName.end())
		{
			continue;   // 不在活跃集（已被卸载）。
		}
		FEngineLayer* Layer = It->second;
		if (!PendingRemoveRequests.count(Layer))
		{
			continue;   // 已处理。
		}
		if (Count > 0)
		{
			break;   // 被依赖 → 堆后只会更大，放弃剩余请求。
		}
		// 安全卸载 Layer：移出管线 + 移出请求集合。
		Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), Layer), Pipelines.end());
		ByName.erase(Name);
		PendingRemoveRequests.erase(Layer);

		// 先更新 Layer 依赖者的被依赖数（-1），重入堆以触发连锁卸载 ——
		// 必须在 delete Layer 之前读 GetDependencies()。
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

		// 最后 delete 实例 + FreeLibrary。
		DeleteUnloaded(Layer);
	}

	// 放弃本批无法安全卸载的剩余请求（不跨帧追认）。
	PendingRemoveRequests.clear();
}

} // namespace Maho
