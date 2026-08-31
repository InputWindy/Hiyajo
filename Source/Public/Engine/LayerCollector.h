#pragma once

#include <Core/Assembly.h>
#include <Core/Fatal.h>
#include <Engine/Layer.h>
#include <Engine/LayerTaskGraph.h>
#include <Engine/Query.h>

#include <algorithm>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Maho
{

// ── FLayerCollector: layer-collection management base ─────────────────────

/**
 * Owns + schedules a set of anonymous FLayerBase instances. Install/Uninstall
 * are recorded into pending sets and applied at the FlushPendingUpdatePipelines
 * safe point; unload is dependency-safe (min-heap greedy). The init/tick/
 * shutdown stage lists are caller-supplied (FEngineBase uses the engine stages,
 * a domain subsystem like FRender uses its own).
 *
 * TContext is the scheduling context passed to every stage method (FEngineBase
 * for the engine, FRender for the render subsystem). It also supplies the
 * FQuery data source (GetQueryData -> Pipelines).
 */
template <typename TContext>
class FLayerCollector : public virtual FQuery<FLayerBase>
{
public:
	/** Active layer instances (read-only). */
	const std::vector<std::unique_ptr<FLayerBase>>& GetLayers() const
	{
		return Features;
	}

	/** Install sugar: register a layer instance (takes effect next safe point). */
	void Install(FLayerBase* Pipeline)
	{
		PendingAdded.push_back(Pipeline);
	}

	/** Dynamically load a layer DLL via FAssembly and install it (next safe point). */
	void Install(std::string_view DllPath, const char* FactorySymbol = "CreateLayer")
	{
		auto Asm = std::make_unique<FAssembly>(DllPath);
		if (!Asm->IsLoaded())
		{
			return;
		}

		using CreateFn = FLayerBase* (*)();
		auto Create = Asm->GetProcAs<CreateFn>(FactorySymbol);
		if (Create == nullptr)
		{
			return;
		}

		auto Layer = std::unique_ptr<FLayerBase>(Create());
		if (!Layer)
		{
			return;
		}

		Install(Layer.get());
		Modules.push_back(std::move(Asm));
		Features.push_back(std::move(Layer));
	}

	/** Request a layer unload (unconditionally recorded, no immediate validation). */
	void RequestUninstall(FLayerBase* Pipeline)
	{
		if (Pipeline != nullptr)
		{
			PendingRemoveRequests.insert(Pipeline);
		}
	}

	/** Anonymous unload by layer name (GetName()); ignored when absent. */
	void TryUninstall(std::string_view LayerName)
	{
		for (FLayerBase* L : Pipelines)
		{
			if (L->GetName() == LayerName)
			{
				RequestUninstall(L);
				return;
			}
		}
	}

protected:
	// -- FQuery data source --
	std::vector<FLayerBase*>& GetQueryData() override { return Pipelines; }
	const std::vector<FLayerBase*>& GetQueryData() const override { return Pipelines; }

	/** Apply pending installs (driving Init stages) + pending uninstalls (driving Shutdown stages). */
	template <typename... TInitStages>
	void FlushPendingUpdatePipelines()
	{
		if (!PendingAdded.empty())
		{
			std::vector<FLayerBase*> NewLayers;
			NewLayers.reserve(PendingAdded.size());
			for (FLayerBase* P : PendingAdded)
			{
				Pipelines.push_back(P);
				NewLayers.push_back(P);
			}
			PendingAdded.clear();

			using FInitStages = TTypeList<TInitStages...>;
			FLayerTaskGraph<FInitStages, TContext> InitGraph(Pool, GetContext());
			InitGraph.Init(std::move(NewLayers));
			if (!InitGraph.Compile())
			{
				ReportFatal("FLayerCollector: install init pipeline Compile failed");
			}
			InitGraph.Execute();
			InitGraph.Flush();
		}

		FlushUnload<TInitStages...>();
	}

	/** The engine owns feature instances + DLLs; on unload it deletes + FreeLibrary them together. */
	void DeleteUnloaded(FLayerBase* Layer)
	{
		for (std::size_t I = 0; I < Features.size(); ++I)
		{
			if (Features[I].get() == Layer)
			{
				Features[I].reset();
				if (I < Modules.size())
				{
					Modules[I].reset();
				}
				return;
			}
		}
	}

private:
	TContext& GetContext() { return *static_cast<TContext*>(this); }

	/** Rebuild the reverse dependency count: layer name -> depended-on count. */
	void RebuildReverseDeps()
	{
		ReverseDepCount.clear();

		for (FLayerBase* L : Pipelines)
		{
			ReverseDepCount[std::string(L->GetName())] = 0;
		}
		for (FLayerBase* L : PendingAdded)
		{
			ReverseDepCount[std::string(L->GetName())] = 0;
		}

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
		for (FLayerBase* L : PendingAdded)
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

	/** Min-heap greedy unload, then drive the Shutdown stages before delete. */
	template <typename... TShutdownStages>
	void FlushUnload()
	{
		if (PendingRemoveRequests.empty())
		{
			return;
		}
		RebuildReverseDeps();

		std::map<std::string, FLayerBase*> ByName;
		for (FLayerBase* L : Pipelines)
		{
			ByName[std::string(L->GetName())] = L;
		}

		using HeapEntry = std::pair<int, std::string>;
		auto Cmp = [](const HeapEntry& A, const HeapEntry& B) { return A.first > B.first; };
		std::priority_queue<HeapEntry, std::vector<HeapEntry>, decltype(Cmp)> Heap(Cmp);
		for (FLayerBase* L : PendingRemoveRequests)
		{
			const std::string Name = std::string(L->GetName());
			Heap.push({ ReverseDepCount[Name], Name });
		}

		std::vector<FLayerBase*> ToUnload;
		while (!Heap.empty())
		{
			const auto [Count, Name] = Heap.top();
			Heap.pop();

			if (ReverseDepCount[Name] != Count)
			{
				continue;
			}
			auto It = ByName.find(Name);
			if (It == ByName.end())
			{
				continue;
			}
			FLayerBase* Layer = It->second;
			if (!PendingRemoveRequests.count(Layer))
			{
				continue;
			}
			if (Count > 0)
			{
				break;
			}
			ByName.erase(Name);
			PendingRemoveRequests.erase(Layer);
			ToUnload.push_back(Layer);

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

		PendingRemoveRequests.clear();

		if (ToUnload.empty())
		{
			return;
		}

		using FShutdownStages = TTypeList<TShutdownStages...>;
		FLayerTaskGraph<FShutdownStages, TContext> ShutdownGraph(Pool, GetContext());
		ShutdownGraph.Init(std::move(ToUnload));
		if (!ShutdownGraph.Compile())
		{
			ReportFatal("FLayerCollector: unload shutdown pipeline Compile failed");
		}
		ShutdownGraph.Execute();
		ShutdownGraph.Flush();

		for (FLayerBase* L : ToUnload)
		{
			Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), L), Pipelines.end());
			DeleteUnloaded(L);
		}
	}

protected:
	std::vector<FLayerBase*> Pipelines;               // active layers (anonymous)
	std::vector<FLayerBase*> PendingAdded;            // pending installs
	std::set<FLayerBase*>    PendingRemoveRequests;   // pending uninstall requests
	std::map<std::string, int> ReverseDepCount;       // layer name -> depended-on count
	std::vector<std::unique_ptr<FAssembly>> Modules;  // DLL keep-alive (move-only)
	std::vector<std::unique_ptr<FLayerBase>> Features; // layer instance ownership
	FThreadPool Pool;
};

} // namespace Maho
