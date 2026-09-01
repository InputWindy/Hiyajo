#pragma once

#include <Core/Assembly.h>
#include <Core/Delegate.h>
#include <Core/Fatal.h>
#include <Engine/Layer.h>
#include <Engine/LayerTaskGraph.h>
#include <Engine/Query.h>

#include <algorithm>
#include <cstdint>
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

	/** Broadcast whenever the active layer set changes at a safe point. The host
	 *  binds this to re-expand its cached task graph (push, not poll). */
	TMulticastEvent<void()> OnLayersChanged;

	/** Dynamically load a layer DLL via FAssembly and install it (next safe point).
	 *  Layers are ALWAYS loaded by name (anonymous-loading convention) -- there is
	 *  no raw-pointer install. Refuses, with a non-fatal error:
	 *    - a duplicate layer name (one instance per name);
	 *    - a declared dependency on a layer that is not already installed/pending
	 *      (deps first -- a failed install propagates to its dependents).
	 *  Load / symbol / factory failures are REPORTED, never silent.
	 *  Returns true on success. */
	bool Install(std::string_view DllPath, const char* FactorySymbol = "CreateLayer")
	{
		auto Asm = std::make_unique<FAssembly>(DllPath);
		if (!Asm->IsLoaded())
		{
			ReportError((std::string("Install: failed to load module: ") + std::string(DllPath)).c_str());
			return false;
		}

		using CreateFn = FLayerBase* (*)();
		auto Create = Asm->GetProcAs<CreateFn>(FactorySymbol);
		if (Create == nullptr)
		{
			ReportError((std::string("Install: module exports no '") + FactorySymbol
				+ "': " + std::string(DllPath)).c_str());
			return false;
		}

		auto Layer = std::unique_ptr<FLayerBase>(Create());
		if (!Layer)
		{
			ReportError((std::string("Install: factory returned null: ") + std::string(DllPath)).c_str());
			return false;
		}

		const std::string_view Name = Layer->GetName();

		// One instance per name -- a duplicate would silently shadow the old one.
		if (HasLayerName(Name))
		{
			ReportError((std::string("Install refused: layer already active with name '")
				+ std::string(Name) + "' (one instance per name)").c_str());
			return false;
		}

		// Fail-fast dependency check, symmetric to unload's refusal: every declared
		// dep must already be installed or pending. If this layer's install fails,
		// no dependent can install either -- the failure propagates cleanly instead
		// of leaving a dangling dependency for the graph to trip over.
		for (const auto& [Stage, Deps] : Layer->GetDependencies())
		{
			(void)Stage;
			for (const auto& Dep : Deps)
			{
				if (!HasLayerName(Dep.Name))
				{
					ReportError((std::string("Install refused: layer '") + std::string(Name)
						+ "' depends on unknown layer '" + Dep.Name
						+ "' (install its dependencies first)").c_str());
					return false;
				}
			}
		}

		PendingAdded.push_back(Layer.get());
		Modules.push_back(std::move(Asm));
		Features.push_back(std::move(Layer));
		ModulePaths.push_back(std::string(DllPath));
		return true;
	}

	/** Hot reload: uninstall a layer (by name, dependency-safe) at the next safe
	 *  point, then re-install a fresh copy of its DLL the frame after (old
	 *  module freed before the new one loads). Refused + reported when the layer
	 *  is still depended on. */
	void Reload(std::string_view LayerName)
	{
		for (FLayerBase* L : Pipelines)
		{
			if (L->GetName() != LayerName)
			{
				continue;
			}
			// Find the DLL path this layer was loaded from. The collector owns the
			// load lifecycle, so it stores the path (parallel to Modules/Features);
			// pointer-installed layers have no module to reload.
			for (std::size_t I = 0; I < Features.size(); ++I)
			{
				if (Features[I].get() != L)
				{
					continue;
				}
				const std::string Path = (I < ModulePaths.size()) ? ModulePaths[I] : std::string{};
				if (Path.empty())
				{
					ReportError((std::string("Reload: layer has no module path (installed by pointer): ")
						+ std::string(LayerName)).c_str());
					return;
				}
				PendingReloads.emplace_back(LayerName, Path);
				RequestUninstall(L);
				return;
			}
			ReportError((std::string("Reload: layer was installed by pointer, nothing to reload: ")
				+ std::string(LayerName)).c_str());
			return;
		}
		ReportError((std::string("Reload: no active layer named ") + std::string(LayerName)).c_str());
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

	/** Apply pending installs (driving Init stages) + pending uninstalls (driving
	 *  Shutdown stages). Broadcasts OnLayersChanged when anything changed so the
	 *  host knows to re-expand its cached graph. */
	template <typename... TInitStages>
	void FlushPendingUpdatePipelines()
	{
		bool bChanged = false;
		if (!PendingAdded.empty())
		{
			bChanged = true;
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
				// Non-fatal: report; the broken layer never initializes until the
				// topology is fixed. No eject heuristic -- the host tick graph
				// re-validates and reports once.
				ReportError((std::string("install init graph compile failed (layer '")
					+ InitGraph.GetCompileErrorNode() + "' has a bad dependency)").c_str());
			}
			else
			{
				InitGraph.Execute();
				InitGraph.Flush();
			}
		}

		if (FlushUnload<TInitStages...>())
		{
			bChanged = true;
		}

		if (bChanged)
		{
			OnLayersChanged.Broadcast();
		}
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
				if (I < ModulePaths.size())
				{
					ModulePaths[I].clear();
				}
				return;
			}
		}
	}

private:
	bool HasLayerName(std::string_view Name) const
	{
		for (const FLayerBase* L : Pipelines)
		{
			if (L->GetName() == Name)
			{
				return true;
			}
		}
		for (const FLayerBase* L : PendingAdded)
		{
			if (L->GetName() == Name)
			{
				return true;
			}
		}
		return false;
	}

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

	/** Min-heap greedy unload, then drive the Shutdown stages before delete.
	 *  Returns true when any layer was actually unloaded. */
	template <typename... TShutdownStages>
	bool FlushUnload()
	{
		if (PendingRemoveRequests.empty())
		{
			return false;
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
			return false;
		}

		using FShutdownStages = TTypeList<TShutdownStages...>;
		FLayerTaskGraph<FShutdownStages, TContext> ShutdownGraph(Pool, GetContext());
		ShutdownGraph.Init(std::move(ToUnload));
		if (!ShutdownGraph.Compile())
		{
			ReportError("FLayerCollector: unload shutdown pipeline Compile failed");
		}
		else
		{
			ShutdownGraph.Execute();
			ShutdownGraph.Flush();
		}

		for (FLayerBase* L : ToUnload)
		{
			Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), L), Pipelines.end());
			DeleteUnloaded(L);
		}

		// Hot reload: the old instance + module are now freed -- load a fresh
		// copy of each reloaded layer. Its Init runs at the next safe point.
		if (!PendingReloads.empty())
		{
			std::set<std::string> UnloadedNames;
			for (FLayerBase* L : ToUnload)
			{
				UnloadedNames.insert(std::string(L->GetName()));
			}
			for (const auto& [Name, Path] : PendingReloads)
			{
				if (UnloadedNames.count(Name))
				{
					Install(Path);
				}
				else
				{
					ReportError((std::string("Reload refused (layer still depended on or absent): ")
						+ Name).c_str());
				}
			}
			PendingReloads.clear();
		}

		return true;
	}

protected:
	std::vector<FLayerBase*> Pipelines;               // active layers (anonymous)
	std::vector<FLayerBase*> PendingAdded;            // pending installs
	std::set<FLayerBase*>    PendingRemoveRequests;   // pending uninstall requests
	std::vector<std::pair<std::string, std::string>> PendingReloads;  // (name, dll path)
	std::map<std::string, int> ReverseDepCount;       // layer name -> depended-on count
	std::vector<std::unique_ptr<FAssembly>> Modules;  // DLL keep-alive (move-only)
	std::vector<std::string> ModulePaths;             // parallel to Modules/Features: DLL path per layer
	std::vector<std::unique_ptr<FLayerBase>> Features; // layer instance ownership
	FThreadPool Pool;                                 // task execution
};

} // namespace Maho
