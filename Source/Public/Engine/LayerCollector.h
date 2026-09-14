#pragma once

#include <Core/Assembly.h>
#include <Core/Delegate.h>
#include <Core/Fatal.h>
#include <Engine/Layer.h>
#include <Engine/LayerTaskGraph.h>
#include <Engine/PluginManager.h>
#include <Engine/Query.h>

#include <algorithm>
#include <atomic>
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

	/** Broadcast whenever the active layer set changes at a safe point. The host
	 *  binds this to re-expand its cached task graph (push, not poll). */
	TMulticastEvent<void()> OnLayersChanged;

	/** Typed install: install a plugin by its layer type, resolving the DLL path
	 *  from T::GetModulePath() (the layer type knows its own module). Equivalent
	 *  to Install("T's dll"). Use this wherever the type is visible. */
	template <typename T>
	bool Install()
	{
		return Install(std::string(T::GetModulePath()));
	}

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
		// Refused once the collection is closing: loading a module then is never right
		// (its stages would never run and its DLL would outlive the teardown order), so
		// it is refused LOUDLY instead of being silently dropped later.
		if (IsClosing())
		{
			ReportError((std::string("Install refused: the collection is closing (") + std::string(DllPath) + ")").c_str());
			return false;
		}

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

		// No fail-fast dependency refusal here: features may legitimately reference
		// each other cross-stage in ANY install order (e.g. Scene.Present depends on
		// DrawTriangle.Render while DrawTriangle.Render depends on Scene.Render).
		// A per-name check cannot see that the cycle is valid across stages, so it
		// would refuse valid mutual deps. The stage-aware graph Compile validates
		// missing deps / real cycles instead (and reports once, non-fatal).

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
		if (IsClosing())
		{
			ReportError((std::string("Reload refused: the collection is closing (") + std::string(LayerName) + ")").c_str());
			return;
		}

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

	/** Anonymous unload of ONE layer. Accepts a query identifying it, matching the FIRST
	 *  layer whose GetName() equals it (e.g. "FScene") OR whose installed DLL path
	 *  equals it (e.g. "EditorConsole.dll") -- the latter is symmetric with
	 *  Install("...dll"). A pointer-installed layer has no DLL path, so it matches only
	 *  by name. Ignored when absent (no error).
	 *
	 *  DIRECT layers only, mirroring InstallChildrenOf: each layer installs its own
	 *  children into its own collector and uninstalls them in its own Shutdown, so
	 *  uninstalling a parent must NOT walk the catalog tree below it. Doing that looked
	 *  up child names in THIS collector's active set, where a parent's children never
	 *  live (they live in the parent's collector) -- dead work at best, and a
	 *  name-collision hazard across collectors at worst (two instances of one layer type
	 *  in different collectors are legal here). */
	void TryUninstall(std::string_view Query)
	{
		// 1) Exact layer name (GetName()) -- the pre-existing form; callers like
		//    GameWorld/Render pass L->GetName() and must keep working.
		for (FLayerBase* L : Pipelines)
		{
			if (L->GetName() == Query)
			{
				RequestUninstall(L);
				return;
			}
		}
		// 2) DLL/module path (symmetry with Install("...dll")); ModulePaths is
		//    parallel to Features and holds the exact string passed to Install.
		for (std::size_t I = 0; I < Features.size(); ++I)
		{
			if (Features[I] && I < ModulePaths.size() && ModulePaths[I] == Query)
			{
				RequestUninstall(Features[I].get());
				return;
			}
		}
	}

	/** Install the catalog's DIRECT children of a node into THIS collector. This is
	 *  the one call the host and every collector layer share -- identical install
	 *  code, because each node already knows its own name:
	 *    host:      InstallChildrenOf(GetName())   // MAHO_DECLARE_ENGINE's GetName()
	 *    collector: InstallChildrenOf(GetName())   // FLayerBase::GetName()
	 *  Child DLLs are loaded by module base name via Install(DllPath) -- never linked,
	 *  always runtime-loaded into this collector. No-op when the node has no children.
	 *
	 *  Direct children only, deliberately: a child that has children of its own
	 *  installs them itself (same call, its own collector), so no level has to know
	 *  about the one below it and nothing can be installed twice. */
	void InstallChildrenOf(std::string_view ParentLayer)
	{
		for (const std::string& Child : FPluginManager::Get().GetChildren(ParentLayer))
		{
			Install(ApplyModuleExtension(Child));
		}
	}

protected:
	// -- FQuery data source --
	std::vector<FLayerBase*>& GetQueryData() override { return Pipelines; }
	const std::vector<FLayerBase*>& GetQueryData() const override { return Pipelines; }

	/** Apply pending installs (driving Init stages) + pending uninstalls (driving
	 *  Shutdown stages). Broadcasts OnLayersChanged when anything changed so the
	 *  host knows to re-expand its cached graph. */
	// TInitStages / TShutdownStages are TTypeList<> stage lists: the first drives
	// the install-init graph, the second the unload-shutdown graph. A layer's
	// install and teardown stages are DIFFERENT interfaces, so passing one pack to
	// both would re-run init methods during unload.
	template <typename TInitStages, typename TShutdownStages>
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

			FLayerTaskGraph<TInitStages, TContext> InitGraph(Pool, GetContext());
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

		if (FlushUnload<TShutdownStages>())
		{
			bChanged = true;
		}

		if (bChanged)
		{
			OnLayersChanged.Broadcast();
		}
	}

	/** Discard installs that were queued but never applied. Install() loads the module
	 *  and builds the instance RIGHT AWAY (only the init stages are deferred), so
	 *  dropping the queue is not enough: the instance and its module have to be
	 *  released too -- otherwise they survive to ~FLayerCollector, i.e. past every
	 *  other layer's unload, and freeing a module whose dependencies are already gone
	 *  is exactly where a detach crash lives. */
	void DropPendingInstalls()
	{
		for (FLayerBase* Layer : PendingAdded)
		{
			DeleteUnloaded(Layer);
		}
		PendingAdded.clear();
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
	 *  Returns true when any layer was actually unloaded. TShutdownStages is a
	 *  TTypeList<> of teardown stage interfaces -- NOT the init stages. */
	template <typename TShutdownStages>
	bool FlushUnload()
	{
		TraceTeardown((std::string("FlushUnload enter requests=") + std::to_string(PendingRemoveRequests.size())
			+ " pipelines=" + std::to_string(Pipelines.size())).c_str());
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

		TraceTeardown((std::string("  toUnload=") + std::to_string(ToUnload.size())).c_str());
		if (ToUnload.empty())
		{
			return false;
		}

		FLayerTaskGraph<TShutdownStages, TContext> ShutdownGraph(Pool, GetContext());
		// Copy, do NOT move: ToUnload is still needed below to erase the layers from
		// Pipelines and to release their instances + modules. Moving it here left the
		// loop below iterating an empty vector -- the Shutdown stages ran, but nothing
		// was ever destroyed, so every layer's DLL was released last (after its own
		// dependencies were already unloaded).
		ShutdownGraph.Init(ToUnload);
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
		TraceTeardown((std::string("  pipelines left=") + std::to_string(Pipelines.size())).c_str());

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

	/** Request a layer unload (unconditionally recorded, no immediate validation). */
	void RequestUninstall(FLayerBase* Pipeline)
	{
		if (Pipeline != nullptr)
		{
			TraceTeardown((std::string("  request uninstall ") + std::string(Pipeline->GetName())).c_str());
			PendingRemoveRequests.insert(Pipeline);
		}
	}

	/** The engine owns feature instances + DLLs; on unload it deletes + FreeLibrary them together. */
	void DeleteUnloaded(FLayerBase* Layer)
	{
		for (std::size_t I = 0; I < Features.size(); ++I)
		{
			if (Features[I].get() == Layer)
			{
				TraceTeardown((std::string("  destroy instance ") + std::string(Layer->GetName())).c_str());
				Features[I].reset();
				TraceTeardown("  release module");
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

protected:
	/** The one "this collection is closing" flag, owned here because it answers for both
	 *  sides of it:
	 *    - Install / Reload REFUSE once it is set -- a module loaded while the collection
	 *      comes down would never have its stages run, and its DLL would outlive the
	 *      teardown order;
	 *    - the host derives its own vocabulary from it (an `IExit` stage calls the
	 *      engine's RequestExit, and the main loop reads the engine's ShouldExit).
	 *  Atomic: it is set from a stage (any thread) and read from the loop and the guards. */
	std::atomic<bool> bClosing{ false };

	[[nodiscard]] bool IsClosing() const noexcept { return bClosing.load(std::memory_order_acquire); }

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
